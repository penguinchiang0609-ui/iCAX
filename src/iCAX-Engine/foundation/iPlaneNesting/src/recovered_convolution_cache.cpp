#include "recovered_convolution_cache.hpp"
#include "recovered_convolution_floating.hpp"
#include "recovered_convolution_symmetry.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
namespace study::recovered {
namespace {
int32_t add32(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t sub32(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int32_t footprint(const ConvolutionBoundaryData &d){return std::bit_cast<int32_t>(uint32_t(d.records.size())*56U+32U);}
}
bool convolutionKeyLess(const ConvolutionKey &a,const ConvolutionKey &b){
    for(size_t n=0;n<2;++n){if(convolutionPoseLess(a.poses[n],b.poses[n]))return true;if(convolutionPoseLess(b.poses[n],a.poses[n]))return false;}return a.expansion<b.expansion;
}
uint64_t convolutionReferenceHash(double value){const uint64_t bits=value==0?0:std::bit_cast<uint64_t>(value);uint64_t h=0xcbf29ce484222325;for(size_t n=0;n<8;++n)h=(h^((bits>>(8*n))&255))*0x100000001b3;return h;}
bool convolutionGeometryEqual(std::span<const ShakerPoint> a,std::span<const ShakerPoint> b){if(a.size()!=b.size())return false;for(size_t n=0;n<a.size();++n)if(a[n][0]!=b[n][0]||a[n][1]!=b[n][1])return false;return true;}
uint64_t convolutionGeometryHash(const ConvolutionGeometry &geometry){
    // Point storage is hashed byte-for-byte, including signed zero and NaN
    // payloads. Only the two reference coordinates normalize signed zero.
    const auto mix=[](uint64_t h,uint64_t word){for(size_t n=0;n<8;++n)h=(h^((word>>(n*8))&255))*0x100000001b3;return h;};
    uint64_t points=0xcbf29ce484222325;
    for(const auto &p:geometry.points)for(double coordinate:p)points=mix(points,std::bit_cast<uint64_t>(coordinate));
    uint64_t result=mix(0xcbf29ce484222325,points);
    for(double coordinate:geometry.reference)result=mix(result,convolutionReferenceHash(coordinate));
    return result;
}
ConvolutionGeometryCatalog::Geometry ConvolutionGeometryCatalog::canonicalize(Geometry geometry){
    std::lock_guard lock(mutex_);const auto identity=geometry.get();
    if(auto i=state_.aliases.find(identity);i!=state_.aliases.end())return i->second;
    retained_.try_emplace(identity,geometry);
    auto h=state_.hashes.find(identity);
    if(h==state_.hashes.end())h=state_.hashes.emplace(identity,convolutionGeometryHash(*geometry)).first;
    auto [r,inserted]=state_.representatives.try_emplace(h->second,geometry);
    if(!inserted&&r->second.get()!=identity){
        const auto &other=*r->second;
        // The original keeps only the first representative per hash. Failed
        // equality is not memoized in the alias table (not even for NaNs).
        if(geometry->reference[0]!=other.reference[0]||geometry->reference[1]!=other.reference[1]||!convolutionGeometryEqual(geometry->points,other.points))return geometry;
        geometry=r->second;
    }
    state_.aliases.emplace(identity,geometry);return geometry;
}
ConvolutionGeometryCatalog::Snapshot ConvolutionGeometryCatalog::snapshot()const{std::lock_guard lock(mutex_);return state_;}
ConvolutionSymmetry convolutionPoseSymmetry(const ConvolutionPose &p){
    // CVTTSD2SI is 64-bit, then the original takes the low signed 32 bits
    // before modulo. Large finite angles must not be reduced as doubles.
    double scaled=(p.angle*57.29577951308232)*10000.;scaled=scaled>=0?scaled+.5:scaled-.5;
    const int64_t truncated=(!std::isfinite(scaled)||scaled>=9223372036854775808.||scaled<-9223372036854775808.)?INT64_MIN:int64_t(scaled);
    int32_t angle=std::bit_cast<int32_t>(uint32_t(truncated))%3600000;if(angle<0)angle+=3600000;
    const bool mx=p.mirror[0]!=0,my=p.mirror[1]!=0;if(my)angle=(angle+1800000)%3600000;
    return {1,false,mx!=my,angle,p.geometry->key};
}
ConvolutionCache::ConvolutionCache(ConvolutionCacheAccounting &a,std::function<int64_t()> clock100ns,int32_t initialBytes):accounting_(a),clock_(std::move(clock100ns)),entries_(UINT32_MAX,[](const Value &a,const Value &b){return convolutionKeyLess(a->key,b->key);}),bytes_(initialBytes){}
void ConvolutionCache::touch(const std::shared_ptr<ConvolutionBoundaryData> &data){data->lastAccess.exchange(clock_()/10000);}
std::shared_ptr<ConvolutionBoundaryData> ConvolutionCache::get(const ConvolutionKey &key){
    std::shared_lock lock(mutex_);const auto value=entries_.findEquivalent(std::make_shared<Entry>(Entry{key,{}}));if(!value)return {};touch((*value)->data);return (*value)->data;
}
std::shared_ptr<ConvolutionBoundaryData> ConvolutionCache::put(const ConvolutionKey &key,std::shared_ptr<ConvolutionBoundaryData> data){
    std::unique_lock lock(mutex_);auto probe=std::make_shared<Entry>(Entry{key,{}});
    if(auto value=entries_.findEquivalent(probe)){touch((*value)->data);return (*value)->data;}
    int32_t total;{std::shared_lock globalLock(accounting_.mutex);total=accounting_.bytes;}
    int32_t removed=0;
    if(bytes_>0xc800000&&total>0x7d000000&&entries_.size()!=0){
        const auto values=entries_.values();std::vector<int64_t> times;times.reserve(values.size());for(auto &v:values)times.push_back(v->data->lastAccess.load());
        auto nth=times.begin()+times.size()/3;std::nth_element(times.begin(),nth,times.end());const int64_t cutoff=*nth;
        entries_.eraseWhere([&](const Value &v){if(v->data->lastAccess.load()>cutoff)return false;const auto cost=footprint(*v->data);bytes_=sub32(bytes_,cost);removed=add32(removed,cost);return true;});
    }
    auto entry=entries_.findOrInsert(probe);entry->data=data;const auto cost=footprint(*data);bytes_=add32(bytes_,cost);
    {std::unique_lock globalLock(accounting_.mutex);accounting_.bytes=add32(accounting_.bytes,sub32(cost,removed));accounting_.peak=std::max(accounting_.peak,int64_t(accounting_.bytes));}
    touch(data);return data;
}
std::vector<ConvolutionCache::Shape> ConvolutionCache::snapshot()const{std::shared_lock lock(mutex_);return entries_.shape();}
int32_t ConvolutionCache::bytes()const{std::shared_lock lock(mutex_);return bytes_;}
void ConvolutionCache::clear() {
    std::unique_lock lock(mutex_);
    entries_.clear();
    {
        std::unique_lock globalLock(accounting_.mutex);
        accounting_.bytes = sub32(accounting_.bytes, bytes_);
        accounting_.peak = std::max(accounting_.peak, int64_t(accounting_.bytes));
    }
    bytes_ = 0;
}
ConvolutionStraightRuntime::ConvolutionStraightRuntime(ConvolutionCacheAccounting &accounting,std::function<int64_t()> clock100ns,std::function<int64_t()> clockNanoseconds,bool symmetryEnabled):cache_(accounting,clock100ns),polygonCache_(accounting,std::move(clock100ns)),symmetryCache_(std::make_unique<ConvolutionSymmetryCache>()),symmetryEnabled_(symmetryEnabled),clock_(std::move(clockNanoseconds)){}
ConvolutionStraightRuntime::~ConvolutionStraightRuntime()=default;
void ConvolutionStraightRuntime::clear() {
    poses_.clear();
    cache_.clear();
    polygonCache_.clear();
}
std::vector<ShakerBoundary> ConvolutionStraightRuntime::get(ConvolutionKey &key,double &elapsed,ConvolutionBackend backend){
    for(auto &pose:key.poses)pose.geometry=geometries_.canonicalize(pose.geometry);
    return boundary(key,elapsed,backend)->records;
}
std::shared_ptr<ConvolutionBoundaryData> ConvolutionStraightRuntime::boundary(const ConvolutionKey &key,double &elapsed,ConvolutionBackend backend){
    if(backend!=ConvolutionBackend::SourceEdges&&backend!=ConvolutionBackend::IntegerPolygons&&backend!=ConvolutionBackend::SymmetricPolygons)throw std::invalid_argument("Unsupported convolution backend");
    // 722010 selects the cache before expansion forces computation mode 0.
    auto &cache=backend==ConvolutionBackend::SourceEdges?cache_:polygonCache_;
    if(auto cached=cache.get(key))return cached;
    if(backend==ConvolutionBackend::SymmetricPolygons&&!symmetryEnabled_)backend=ConvolutionBackend::IntegerPolygons;
    const double before=double(clock_()/1000000)/1000.;
    if(backend==ConvolutionBackend::SymmetricPolygons&&key.expansion==0){
        const auto sa=convolutionPoseSymmetry(key.poses[0]),sb=convolutionPoseSymmetry(key.poses[1]);
        auto hit=symmetryCache_->get(sa,sb,key.poses[0].geometry,key.poses[1].geometry);
        if(!hit){
            const auto canonical=convolutionCanonicalPair(sa,sb);
            auto first=key.poses[0],second=key.poses[1];first.angle=0;first.mirror={0,0};first.side=0;second.side=0;
            second.angle=(canonical[1].angle/10000.)*0.017453292519943295;second.mirror={uint8_t(canonical[1].mirror),0};
            auto a=poses_.acquire(first,curves_.edgeFactory()),b=poses_.acquire(second,curves_.edgeFactory());
            try{auto polygon=std::make_shared<ConvolutionPolygon>(convolutionFloatingMinkowski(*a,*b));
                poses_.release(first,std::move(a));poses_.release(second,std::move(b));
                symmetryCache_->put(canonical[1],std::move(polygon),first.geometry,second.geometry);
            }catch(...){if(a)poses_.release(first,std::move(a));if(b)poses_.release(second,std::move(b));throw;}
            hit=symmetryCache_->get(sa,sb,first.geometry,second.geometry);
            if(!hit)throw std::logic_error("Convolution symmetry insertion cannot be retrieved");
        }
        auto result=std::make_shared<ConvolutionBoundaryData>();result->records=convolutionPolygonRecords(convolutionTransformCachedPolygon(*hit->polygon,hit->transform));
        result=cache.put(key,std::move(result));elapsed=(double(clock_()/1000000)/1000.-before)+elapsed;return result;
    }
    auto a=poses_.acquire(key.poses[0],curves_.edgeFactory());
    const bool same=!convolutionPoseLess(key.poses[0],key.poses[1])&&!convolutionPoseLess(key.poses[1],key.poses[0]);
    auto b=same?std::unique_ptr<ConvolutionTopology>{}:poses_.acquire(key.poses[1],curves_.edgeFactory());
    try{
        auto result=std::make_shared<ConvolutionBoundaryData>();
        if(backend==ConvolutionBackend::IntegerPolygons&&key.expansion==0){
            result->records=convolutionPolygonRecords(convolutionFloatingMinkowski(*a,same?*a:*b));
        }else{
            auto first=convolutionPolygon(*a),second=convolutionPolygon(same?*a:*b);
            if(key.expansion!=0){for(auto &e:first)convolutionBroaden(e,key.expansion);for(auto &e:second)convolutionBroaden(e,key.expansion);}
            auto pairs=convolutionSweep(first,second);if(key.expansion==0)convolutionLinkPairs(first,second,pairs);
            result->records=convolutionRecords(first,second,pairs,key.expansion==0);
        }
        poses_.release(key.poses[0],std::move(a));if(b)poses_.release(key.poses[1],std::move(b));
        result=cache.put(key,std::move(result));elapsed=(double(clock_()/1000000)/1000.-before)+elapsed;return result;
    }catch(...){if(a)poses_.release(key.poses[0],std::move(a));if(b)poses_.release(key.poses[1],std::move(b));throw;}
}
}
