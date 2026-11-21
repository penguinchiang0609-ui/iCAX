#include "SheetNestingNfp.h"
#include "NestingInternal.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace iCAX::SheetNesting
{
namespace
{
    using Ring=std::vector<Point>;
    using Clock=std::chrono::steady_clock;
    constexpr std::size_t MaxVertices=2048,MaxUnionVertices=65536;
    long double Cross(Point a,Point b,Point c)
    {return (static_cast<long double>(b.X)-a.X)*(static_cast<long double>(c.Y)-a.Y)-(static_cast<long double>(b.Y)-a.Y)*(static_cast<long double>(c.X)-a.X);}
    bool Same(Point a,Point b){return a.X==b.X&&a.Y==b.Y;}
    bool Less(Point a,Point b){return a.X!=b.X?a.X<b.X:a.Y<b.Y;}
    void Word(std::string& key,std::uint64_t word)
    {for(int i=0;i<8;++i)key.push_back(static_cast<char>((word>>(8*i))&255));}
    void Number(std::string& key,double value)
    {if(value==0)value=0;Word(key,std::bit_cast<std::uint64_t>(value));}
    void DifferenceNumber(std::string& key,double a,double b)
    {
        // Error-free floating difference key: translation normalization must
        // not collapse distinct geometry through cancellation/rounding.
        const double high=a-b,bvirtual=a-high,avirtual=high+bvirtual;
        const double low=(a-avirtual)+(bvirtual-b);
        Number(key,high);Number(key,low);
    }
    double Angle(double angle)
    {angle=std::fmod(angle,360.);if(angle<0)angle+=360.;return angle==0?0:angle;}
    struct Budget
    {
        const NfpBuildOptions& Options;
        Clock::time_point Started=Clock::now();
        NfpStatus State=NfpStatus::Ready;
        bool Stop()
        {
            if(State!=NfpStatus::Ready)return true;
            if(Options.Cancel&&Options.Cancel())State=NfpStatus::Cancelled;
            else if(std::chrono::duration<double>(Clock::now()-Started).count()>=Options.TimeLimitSeconds)State=NfpStatus::LimitReached;
            return State!=NfpStatus::Ready;
        }
    };
    bool Clean(Ring& ring,Budget* budget=nullptr)
    {
        if(ring.size()>1&&Same(ring.front(),ring.back()))ring.pop_back();
        std::size_t scanned=0;
        bool changed=true;
        while(changed&&ring.size()>3)
        {
            changed=false;
            for(std::size_t i=0;i<ring.size();++i)
            {
                if(budget&&((++scanned&127)==0)&&budget->Stop())return false;
                const auto a=ring[(i+ring.size()-1)%ring.size()],b=ring[i],c=ring[(i+1)%ring.size()];
                if(Cross(a,b,c)==0&&(static_cast<long double>(b.X)-a.X)*(static_cast<long double>(c.X)-b.X)+
                    (static_cast<long double>(b.Y)-a.Y)*(static_cast<long double>(c.Y)-b.Y)>=0)
                {ring.erase(ring.begin()+i);changed=true;break;}
            }
        }
        return true;
    }
    long double SignedArea(const Ring& ring)
    {
        long double area=0;
        for(std::size_t i=1;i+1<ring.size();++i)area+=Cross(ring[0],ring[i],ring[i+1]);
        return area/2;
    }
    struct Geometry {std::string Key;Ring Outer;NfpConvexDecomposition Decomposition;};
    struct Canonical {std::string Key;Ring Outer;Point Origin;};
    void MarkMirrored(Canonical& canonical)
    {
        std::string key;Word(key,std::numeric_limits<std::uint64_t>::max()-1);
        key+=canonical.Key;canonical.Key=std::move(key);
    }
    std::string RegionKey(const Canonical& a,const Canonical& b,double alpha,double beta)
    {
        std::string key;key.reserve(a.Key.size()+b.Key.size()+32);
        Word(key,a.Key.size());key+=a.Key;Word(key,b.Key.size());key+=b.Key;Number(key,Angle(beta-alpha));
        const double delta=beta-alpha,bvirtual=beta-delta,avirtual=delta+bvirtual;
        Number(key,(beta-avirtual)+(bvirtual-alpha));return key;
    }
    bool Canonicalize(const Shape& shape,Canonical& out,Budget& budget)
    {
        if(shape.Radius!=0||!shape.Holes.empty())return false;
        out.Outer=Detail::MaterialRing(shape.Outer);
        if(out.Outer.size()<3)return false;
        out.Origin=out.Outer.front();
        std::size_t scanned=0;
        for(auto p:out.Outer)
        {
            if(((++scanned&127)==0)&&budget.Stop())return false;
            if(!std::isfinite(p.X)||!std::isfinite(p.Y)||std::abs(p.X)>1e7||std::abs(p.Y)>1e7)return false;
            out.Origin.X=std::min(out.Origin.X,p.X);out.Origin.Y=std::min(out.Origin.Y,p.Y);
        }
        if(SignedArea(out.Outer)<0)std::reverse(out.Outer.begin(),out.Outer.end());
        const auto first=std::min_element(out.Outer.begin(),out.Outer.end(),Less);
        std::rotate(out.Outer.begin(),first,out.Outer.end());
        // Key every distinct source vertex before collinear cleanup. Short
        // edges between different points keep their own key and cold-path
        // validity checks. Exact repeated vertices share the same material.
        Word(out.Key,out.Outer.size());for(auto p:out.Outer)
        {
            if(((++scanned&127)==0)&&budget.Stop())return false;
            DifferenceNumber(out.Key,p.X,out.Origin.X);DifferenceNumber(out.Key,p.Y,out.Origin.Y);
        }
        for(auto& p:out.Outer)
        {
            if(((++scanned&127)==0)&&budget.Stop())return false;
            p.X-=out.Origin.X;p.Y-=out.Origin.Y;
        }
        if(!Clean(out.Outer,&budget)||out.Outer.size()<3||out.Outer.size()>MaxVertices)return false;
        return true;
    }
    bool Convex(const Ring& ring)
    {
        for(std::size_t i=0;i<ring.size();++i)
            if(Cross(ring[i],ring[(i+1)%ring.size()],ring[(i+2)%ring.size()])<0)return false;
        return true;
    }
    bool DecomposeGeometry(Geometry& geometry,Budget& budget)
    {
        auto& pieces=geometry.Decomposition.Pieces;
        if(Convex(geometry.Outer)){pieces={geometry.Outer};return true;}
        Ring work=geometry.Outer;
        while(work.size()>3)
        {
            if(budget.Stop())return false;bool clipped=false;
            for(std::size_t i=0;i<work.size();++i)
            {
                if((i&63)==63&&budget.Stop())return false;
                const auto before=(i+work.size()-1)%work.size(),after=(i+1)%work.size();
                const auto a=work[before],b=work[i],c=work[after];if(Cross(a,b,c)<=0)continue;
                bool contains=false;
                for(std::size_t j=0;j<work.size();++j)
                {
                    if((j&63)==63&&budget.Stop())return false;
                    if(j!=before&&j!=i&&j!=after&&Cross(a,b,work[j])>=0&&Cross(b,c,work[j])>=0&&
                        Cross(c,a,work[j])>=0){contains=true;break;}
                }
                if(contains)continue;
                pieces.push_back({a,b,c});work.erase(work.begin()+i);clipped=true;break;
            }
            if(!clipped)return false;
        }
        pieces.push_back(work);
        // Merge exactly adjacent triangles whenever their union is convex.
        // No area tolerance or replacement hull can fill a genuine concavity.
        bool merged=true;
        while(merged)
        {
            merged=false;
            for(std::size_t i=0;i<pieces.size()&&!merged;++i)
                for(std::size_t j=i+1;j<pieces.size()&&!merged;++j)
                {
                    if(budget.Stop())return false;
                    const auto& a=pieces[i];const auto& b=pieces[j];
                    for(std::size_t ai=0;ai<a.size()&&!merged;++ai)
                    {
                        if((ai&63)==63&&budget.Stop())return false;
                        for(std::size_t bi=0;bi<b.size()&&!merged;++bi)
                        {
                            if((bi&63)==63&&budget.Stop())return false;
                            if(Same(a[ai],b[(bi+1)%b.size()])&&Same(a[(ai+1)%a.size()],b[bi]))
                            {
                                Ring joined;
                                for(std::size_t k=0;k<a.size();++k)joined.push_back(a[(ai+1+k)%a.size()]);
                                for(std::size_t k=1;k+1<b.size();++k)joined.push_back(b[(bi+1+k)%b.size()]);
                                if(!Clean(joined,&budget))return false;
                                if(!Convex(joined))continue;
                                pieces[i]=std::move(joined);pieces.erase(pieces.begin()+j);merged=true;
                            }
                        }
                    }
                }
        }
        return true;
    }
    Ring ConvexSum(Ring a,Ring b,Budget& budget)
    {
        const auto start=[](Ring& ring)
        {
            const auto it=std::min_element(ring.begin(),ring.end(),[](Point x,Point y){return x.Y!=y.Y?x.Y<y.Y:x.X<y.X;});
            std::rotate(ring.begin(),it,ring.end());
        };
        start(a);start(b);Ring out;out.reserve(a.size()+b.size());
        std::size_t i=0,j=0,steps=0;Point point{a[0].X+b[0].X,a[0].Y+b[0].Y};
        const auto half=[](Point v){return v.Y<0||(v.Y==0&&v.X<0);};
        while(i<a.size()||j<b.size())
        {
            if(((++steps&127)==0)&&budget.Stop())return {};
            out.push_back(point);bool takeA=false,takeB=false;
            Point da{},db{};
            if(i<a.size())da={a[(i+1)%a.size()].X-a[i].X,a[(i+1)%a.size()].Y-a[i].Y};
            if(j<b.size())db={b[(j+1)%b.size()].X-b[j].X,b[(j+1)%b.size()].Y-b[j].Y};
            if(i==a.size())takeB=true;
            else if(j==b.size())takeA=true;
            else if(half(da)!=half(db)){takeA=half(da)<half(db);takeB=!takeA;}
            else
            {
                const long double cross=static_cast<long double>(da.X)*db.Y-static_cast<long double>(da.Y)*db.X;
                takeA=cross>=0;takeB=cross<=0;
            }
            if(takeA){point.X+=da.X;point.Y+=da.Y;++i;}
            if(takeB){point.X+=db.X;point.Y+=db.Y;++j;}
        }
        if(!Clean(out,&budget))return {};
        return out;
    }
    constexpr std::size_t UnionBatchSize=128,LayeredUnionThreshold=256;
    constexpr std::size_t MaxUnionPayloadBytes=MaxUnionVertices*
        (sizeof(Clipper2Lib::PointD)+sizeof(Clipper2Lib::PathD));
    struct UnionLayerSize
    {
        std::size_t Vertices=0,Bytes=0;
        bool Add(const Clipper2Lib::PathsD& paths)
        {
            if(paths.capacity()>(MaxUnionPayloadBytes-Bytes)/sizeof(Clipper2Lib::PathD))return false;
            Bytes+=paths.capacity()*sizeof(Clipper2Lib::PathD);
            for(const auto& path:paths)
            {
                if(path.size()>MaxUnionVertices-Vertices||
                    path.capacity()>(MaxUnionPayloadBytes-Bytes)/sizeof(Clipper2Lib::PointD))return false;
                Vertices+=path.size();Bytes+=path.capacity()*sizeof(Clipper2Lib::PointD);
            }
            return true;
        }
    };
    bool LayeredUnion(Clipper2Lib::PathsD sums,Clipper2Lib::PathsD& united,Budget& budget)
    {
        using namespace Clipper2Lib;
        UnionLayerSize inputSize;
        if(!inputSize.Add(sums)){budget.State=NfpStatus::LimitReached;return false;}
        std::vector<PathsD> level;
        level.reserve((sums.size()+UnionBatchSize-1)/UnionBatchSize);
        UnionLayerSize outputSize;
        for(std::size_t begin=0;begin<sums.size();begin+=UnionBatchSize)
        {
            if(budget.Stop())return false;
            const auto end=std::min(sums.size(),begin+UnionBatchSize);
            PathsD batch;batch.reserve(end-begin);
            for(auto index=begin;index<end;++index)batch.push_back(std::move(sums[index]));
            auto merged=Union(batch,FillRule::NonZero,8);
            if(!outputSize.Add(merged)){budget.State=NfpStatus::LimitReached;return false;}
            if(budget.Stop())return false;
            level.push_back(std::move(merged));
        }
        while(level.size()>1)
        {
            std::vector<PathsD> next;
            next.reserve((level.size()+1)/2);
            outputSize={};
            for(std::size_t index=0;index<level.size();index+=2)
            {
                if(budget.Stop())return false;
                PathsD merged;
                if(index+1==level.size())merged=std::move(level[index]);
                else
                {
                    auto pair=std::move(level[index]);
                    for(auto& path:level[index+1])pair.push_back(std::move(path));
                    UnionLayerSize pairSize;
                    if(!pairSize.Add(pair)){budget.State=NfpStatus::LimitReached;return false;}
                    merged=Union(pair,FillRule::NonZero,8);
                }
                if(!outputSize.Add(merged)){budget.State=NfpStatus::LimitReached;return false;}
                // Only the last Boolean has consumed every original input.
                // Its immutable complete region may outlive this caller's
                // budget. Earlier layers remain private unfinished work.
                if(budget.Stop()&&level.size()!=2)return false;
                next.push_back(std::move(merged));
            }
            level=std::move(next);
        }
        if(level.empty())return false;
        united=std::move(level.front());return true;
    }
    std::size_t Bytes(const Geometry& g)
    {
        std::size_t n=sizeof(g)+4*sizeof(void*)+g.Key.capacity()+1+g.Outer.capacity()*sizeof(Point)+g.Decomposition.Pieces.capacity()*sizeof(Ring);
        for(const auto& piece:g.Decomposition.Pieces)n+=piece.capacity()*sizeof(Point);return n;
    }
    std::size_t Bytes(const NfpRegion& g)
    {
        std::size_t n=sizeof(g)+4*sizeof(void*)+g.Loops.capacity()*sizeof(Ring);
        for(const auto& loop:g.Loops)n+=loop.capacity()*sizeof(Point);return n;
    }
}
struct NfpPreparedShape::Impl {Shape Source;Canonical Value;};
struct NfpCache::Impl
{
    template<class T>struct Entry{std::shared_ptr<const T> Value;std::size_t Bytes,Tick;double FrameAngle=0;};
    template<class T>static std::size_t EntryBytes(const T& value,const std::string& key)
    {return Bytes(value)+key.size()+1+sizeof(std::string)+sizeof(Entry<T>)+4*sizeof(void*);}
    NfpCacheSettings Settings;mutable std::mutex Mutex;NfpCacheStatistics Stats;std::size_t Tick=0;
    std::unordered_map<std::string,Entry<Geometry>> Shapes;
    std::unordered_map<std::string,Entry<NfpRegion>> Regions;
    struct Flight {std::condition_variable Changed;bool Done=false;};
    std::unordered_map<std::string,std::shared_ptr<Flight>> Flights;
    struct FlightGuard
    {
        Impl& Owner;std::string Key;std::shared_ptr<Flight> Pending;
        FlightGuard(Impl& owner,std::string key,std::shared_ptr<Flight> pending)
            :Owner(owner),Key(std::move(key)),Pending(std::move(pending)){}
        ~FlightGuard()
        {
            {
                std::lock_guard lock(Owner.Mutex);Pending->Done=true;
                const auto it=Owner.Flights.find(Key);
                if(it!=Owner.Flights.end()&&it->second==Pending)Owner.Flights.erase(it);
            }
            Pending->Changed.notify_all();
        }
    };
    explicit Impl(NfpCacheSettings settings):Settings(settings){}
    void Trim()
    {
        while(Shapes.size()>Settings.MaxShapeEntries||Regions.size()>Settings.MaxRegionEntries||Stats.ResidentBytes>Settings.MaxBytes)
        {
            const auto a=std::min_element(Shapes.begin(),Shapes.end(),[](const auto& x,const auto& y){return x.second.Tick<y.second.Tick;});
            const auto b=std::min_element(Regions.begin(),Regions.end(),[](const auto& x,const auto& y){return x.second.Tick<y.second.Tick;});
            const bool shape=a!=Shapes.end()&&(b==Regions.end()||Shapes.size()>Settings.MaxShapeEntries||
                (Regions.size()<=Settings.MaxRegionEntries&&a->second.Tick<b->second.Tick));
            if(shape){Stats.ResidentBytes-=a->second.Bytes;Shapes.erase(a);}
            else if(b!=Regions.end()){Stats.ResidentBytes-=b->second.Bytes;Regions.erase(b);}
            else break;
            ++Stats.Evictions;
        }
    }
    std::shared_ptr<const Geometry> ShapeGeometry(const Shape& source,const Canonical& canonical,Budget& budget,bool validated,bool* cacheHit=nullptr)
    {
        {
            std::lock_guard lock(Mutex);const auto it=Shapes.find(canonical.Key);
            if(it!=Shapes.end()){++Stats.ShapeHits;it->second.Tick=++Tick;if(cacheHit)*cacheHit=true;return it->second.Value;}
            ++Stats.ShapeMisses;
        }
        std::string error;if(!validated&&!Detail::ShapeValid(source,1e-7,error))return {};
        auto geometry=std::make_shared<Geometry>();geometry->Key=canonical.Key;geometry->Outer=canonical.Outer;
        if(!DecomposeGeometry(*geometry,budget))return {};
        {
            std::lock_guard lock(Mutex);const auto it=Shapes.find(canonical.Key);
            if(it!=Shapes.end()){if(cacheHit)*cacheHit=true;return it->second.Value;}
            const auto bytes=EntryBytes(*geometry,canonical.Key);
            if(Settings.MaxShapeEntries&&bytes<=Settings.MaxBytes)
            {Shapes.emplace(canonical.Key,Entry<Geometry>{geometry,bytes,++Tick});Stats.ResidentBytes+=bytes;Trim();}
        }
        return geometry;
    }
    std::shared_ptr<const Geometry> ReflectedRotated(const Geometry& source,double angle,Budget& budget)
    {
        std::string key;Word(key,std::numeric_limits<std::uint64_t>::max());key+=source.Key;Number(key,angle);
        {
            std::lock_guard lock(Mutex);const auto it=Shapes.find(key);
            if(it!=Shapes.end()){++Stats.RotatedHits;it->second.Tick=++Tick;return it->second.Value;}
            ++Stats.RotatedMisses;
        }
        auto transformed=std::make_shared<Geometry>();transformed->Key=key;
        for(const auto& piece:source.Decomposition.Pieces)
        {
            if(budget.Stop())return {};
            Ring rotated;rotated.reserve(piece.size());
            for(auto point:piece){const auto p=Detail::Transform(point,{0,0,angle});rotated.push_back({-p.X,-p.Y});}
            transformed->Decomposition.Pieces.push_back(std::move(rotated));
        }
        {
            std::lock_guard lock(Mutex);const auto it=Shapes.find(key);if(it!=Shapes.end())return it->second.Value;
            const auto bytes=EntryBytes(*transformed,key);
            if(Settings.MaxShapeEntries&&bytes<=Settings.MaxBytes)
            {Shapes.emplace(key,Entry<Geometry>{transformed,bytes,++Tick});Stats.ResidentBytes+=bytes;Trim();}
        }
        return transformed;
    }
    NfpLookup Query(const Shape& a,const Canonical& ca,double alpha,const Shape& b,const Canonical& cb,double beta,Budget& budget,bool validated);
};
NfpCache::NfpCache(NfpCacheSettings settings):Data(std::make_unique<Impl>(settings)){}
NfpCache::~NfpCache()=default;
NfpLookup NfpCache::Get(const Shape& a,double alpha,const Shape& b,double beta,const NfpBuildOptions& options,
    bool stationaryMirrored,bool movingMirrored)
{
    NfpLookup result;Budget budget{options};
    if(!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<0||!std::isfinite(alpha)||!std::isfinite(beta))
    {result.Diagnostic="Invalid NFP angles or build budget.";return result;}
    if(budget.Stop()){result.State=budget.State;return result;}
    const Shape mirroredA=stationaryMirrored?Detail::ReflectedShape(a):Shape{};
    const Shape mirroredB=movingMirrored?Detail::ReflectedShape(b):Shape{};
    const auto& sourceA=stationaryMirrored?mirroredA:a;
    const auto& sourceB=movingMirrored?mirroredB:b;
    Canonical ca,cb;
    if(!Canonicalize(sourceA,ca,budget)||!Canonicalize(sourceB,cb,budget))
    {
        result.State=budget.State==NfpStatus::Ready?NfpStatus::Unsupported:budget.State;
        result.Diagnostic="NFP requires bounded simple polygon exteriors without holes or circles.";return result;
    }
    if(stationaryMirrored)MarkMirrored(ca);
    if(movingMirrored)MarkMirrored(cb);
    return Data->Query(sourceA,ca,alpha,sourceB,cb,beta,budget,false);
}
NfpLookup NfpCache::Impl::Query(const Shape& a,const Canonical& ca,double alpha,const Shape& b,const Canonical& cb,double beta,Budget& budget,bool validated)
{
    NfpLookup result;const auto& options=budget.Options;
    alpha=Angle(alpha);beta=Angle(beta);const double relative=Angle(beta-alpha);
    const auto oa=Detail::Transform(ca.Origin,{0,0,alpha}),ob=Detail::Transform(cb.Origin,{0,0,beta});
    result.Transform={oa.X-ob.X,oa.Y-ob.Y,alpha};
    const auto key=RegionKey(ca,cb,alpha,beta);std::string reverseKey,flightKey;
    std::unique_ptr<Impl::FlightGuard> flight;
    for(;;)
    {
        if(budget.Stop()){result.State=budget.State;return result;}
        std::unique_lock lock(Mutex);const auto it=Regions.find(key);
        if(it!=Regions.end())
        {++Stats.RegionHits;it->second.Tick=++Tick;result.Transform.AngleDegrees=Angle(alpha+it->second.FrameAngle);result.State=NfpStatus::Ready;result.Region=it->second.Value;result.CacheHit=true;return result;}
        if(reverseKey.empty()){reverseKey=RegionKey(cb,ca,beta,alpha);flightKey=std::min(key,reverseKey);}
        // A + (-R_delta B) = -R_delta [B + (-R_-delta A)].
        // Share the reverse pair's immutable loops with a frame transform;
        // never rebuild or copy the polygon union merely to swap A and B.
        const auto reverse=Regions.find(reverseKey);
        if(reverse!=Regions.end())
        {
            ++Stats.RegionHits;++Stats.ReverseHits;reverse->second.Tick=++Tick;
            const double frame=Angle(relative+180+reverse->second.FrameAngle);
            result.Transform.AngleDegrees=Angle(alpha+frame);result.State=NfpStatus::Ready;
            result.Region=reverse->second.Value;result.CacheHit=true;
            const auto bytes=Impl::EntryBytes(*result.Region,key);
            if(Settings.MaxRegionEntries&&bytes<=Settings.MaxBytes)
            {Regions.emplace(key,Impl::Entry<NfpRegion>{result.Region,bytes,++Tick,frame});Stats.ResidentBytes+=bytes;Trim();}
            return result;
        }
        if(const auto pending=Flights.find(flightKey);pending!=Flights.end())
        {
            auto waiting=pending->second;++Stats.CoalescedWaits;
            while(!waiting->Done)
            {
                // A waiter's deadline/cancellation belongs to that query;
                // it cannot cancel the builder or other independent jobs.
                lock.unlock();if(budget.Stop()){result.State=budget.State;return result;}
                lock.lock();if(!waiting->Done)waiting->Changed.wait_for(lock,std::chrono::milliseconds(1));
            }
            continue;
        }
        ++Stats.RegionMisses;
        if(Settings.MaxRegionEntries&&Settings.MaxBytes&&Flights.size()<Settings.MaxRegionEntries)
        {
            auto pending=std::make_shared<Impl::Flight>();
            flight=std::make_unique<Impl::FlightGuard>(*this,flightKey,pending);
            Flights.emplace(flightKey,std::move(pending));
        }
        break;
    }
    const auto ga=ShapeGeometry(a,ca,budget,validated),gb=ShapeGeometry(b,cb,budget,validated);
    if(!ga||!gb||budget.Stop())
    {result.State=budget.State==NfpStatus::Ready?NfpStatus::Unsupported:budget.State;result.Diagnostic="NFP decomposition failed or its budget stopped.";return result;}
    if(ga->Decomposition.Pieces.size()>options.MaxConvexPairs/std::max<std::size_t>(1,gb->Decomposition.Pieces.size()))
    {result.State=NfpStatus::LimitReached;result.Diagnostic="NFP convex-pair limit reached.";return result;}
    const auto moving=ReflectedRotated(*gb,relative,budget);
    if(!moving){result.State=budget.State;return result;}
    {std::lock_guard lock(Mutex);++Stats.RegionBuilds;}
    Clipper2Lib::PathsD sums;std::size_t vertices=0;
    for(const auto& fixed:ga->Decomposition.Pieces)for(const auto& mobile:moving->Decomposition.Pieces)
    {
        if(budget.Stop()){result.State=budget.State;return result;}
        auto sum=ConvexSum(fixed,mobile,budget);
        if(budget.State!=NfpStatus::Ready){result.State=budget.State;return result;}
        vertices+=sum.size();
        if(vertices>MaxUnionVertices){result.State=NfpStatus::LimitReached;result.Diagnostic="NFP union vertex limit reached.";return result;}
        Clipper2Lib::PathD path;path.reserve(sum.size());for(auto point:sum)path.emplace_back(point.X,point.Y);sums.push_back(std::move(path));
    }
    // Convex + convex already yields one complete convex NFP: avoid a Boolean
    // pass and its rounding when there is nothing to merge.
    const auto convexPairs=sums.size();
    const bool layered=convexPairs>LayeredUnionThreshold;
    Clipper2Lib::PathsD united;
    if(layered)
    {
        if(!LayeredUnion(std::move(sums),united,budget))
        {result.State=budget.State;result.Diagnostic="NFP layered union stopped before completing its original input.";return result;}
    }
    else
    {
        united=sums.size()==1?sums:Clipper2Lib::Union(sums,Clipper2Lib::FillRule::NonZero,8);
        if(budget.Stop()){result.State=budget.State;return result;}
    }
    auto region=std::make_shared<NfpRegion>();region->ConvexPairs=convexPairs;
    for(const auto& path:united){Ring loop;loop.reserve(path.size());for(auto point:path)loop.push_back({point.x,point.y});region->Loops.push_back(std::move(loop));}
    if(region->Loops.empty()){result.Diagnostic="NFP union returned no region.";return result;}
    double frame=0;
    {
        std::lock_guard lock(Mutex);const auto it=Regions.find(key);
        if(it!=Regions.end()){region=std::const_pointer_cast<NfpRegion>(it->second.Value);frame=it->second.FrameAngle;}
        else
        {
            const auto bytes=Impl::EntryBytes(*region,key);
            if(Settings.MaxRegionEntries&&bytes<=Settings.MaxBytes)
            {Regions.emplace(key,Impl::Entry<NfpRegion>{region,bytes,++Tick});Stats.ResidentBytes+=bytes;Trim();}
        }
    }
    if(layered&&budget.Stop())
    {result.State=budget.State;result.Diagnostic="NFP union completed after its caller stopped.";return result;}
    result.Transform.AngleDegrees=Angle(alpha+frame);result.State=NfpStatus::Ready;result.Region=std::move(region);return result;
}
NfpPreparedShape NfpCache::Prepare(const Shape& shape,const NfpBuildOptions& options,bool mirrored)
{
    NfpPreparedShape prepared;Budget budget{options};
    if(!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<0)
    {prepared.Diagnostic="Invalid NFP preparation budget.";return prepared;}
    if(budget.Stop()){prepared.State=budget.State;return prepared;}
    auto value=std::make_shared<NfpPreparedShape::Impl>();value->Source=mirrored?Detail::ReflectedShape(shape):shape;
    std::string error;
    if(!Canonicalize(value->Source,value->Value,budget)||!Detail::ShapeValid(value->Source,1e-7,error))
    {
        prepared.State=budget.State==NfpStatus::Ready?NfpStatus::Unsupported:budget.State;
        prepared.Diagnostic="NFP preparation requires a supported simple exterior.";return prepared;
    }
    if(mirrored)MarkMirrored(value->Value);
    if(budget.Stop()){prepared.State=budget.State;return prepared;}
    prepared.State=NfpStatus::Ready;prepared.Data=std::move(value);return prepared;
}
NfpLookup NfpCache::Get(const NfpPreparedShape& a,double alpha,const NfpPreparedShape& b,double beta,const NfpBuildOptions& options)
{
    NfpLookup result;Budget budget{options};
    if(!a||!b||!std::isfinite(alpha)||!std::isfinite(beta)||!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<0)
    {result.Diagnostic="Invalid prepared NFP query.";return result;}
    if(budget.Stop()){result.State=budget.State;return result;}
    return Data->Query(a.Data->Source,a.Data->Value,alpha,b.Data->Source,b.Data->Value,beta,budget,true);
}
NfpDecompositionLookup NfpCache::Decompose(const Shape& shape,const NfpBuildOptions& options)
{
    // Preparation validates the complete original ring before any hot entry
    // can be reused, just as the prepared pair query does.
    const auto started=Clock::now();const auto prepared=Prepare(shape,options);
    if(!prepared){NfpDecompositionLookup result;result.State=prepared.State;result.Diagnostic=prepared.Diagnostic;return result;}
    auto remaining=options;
    remaining.TimeLimitSeconds=std::max(0.,options.TimeLimitSeconds-std::chrono::duration<double>(Clock::now()-started).count());
    return Decompose(prepared,remaining);
}
NfpDecompositionLookup NfpCache::Decompose(const NfpPreparedShape& shape,const NfpBuildOptions& options)
{
    NfpDecompositionLookup result;Budget budget{options};
    if(!shape||!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<0)
    {result.Diagnostic="Invalid prepared convex decomposition query.";return result;}
    if(budget.Stop()){result.State=budget.State;return result;}
    const auto geometry=Data->ShapeGeometry(shape.Data->Source,shape.Data->Value,budget,true,&result.CacheHit);
    if(!geometry||budget.Stop())
    {result.State=budget.State==NfpStatus::Ready?NfpStatus::Unsupported:budget.State;result.Diagnostic="Convex decomposition failed or its budget stopped.";return result;}
    result.SourceOrigin=shape.Data->Value.Origin;
    result.Decomposition=std::shared_ptr<const NfpConvexDecomposition>(geometry,&geometry->Decomposition);
    result.State=NfpStatus::Ready;return result;
}
NfpCacheStatistics NfpCache::Statistics() const
{
    std::lock_guard lock(Data->Mutex);auto stats=Data->Stats;stats.ShapeEntries=Data->Shapes.size();stats.RegionEntries=Data->Regions.size();return stats;
}
void NfpCache::Clear()
{
    std::lock_guard lock(Data->Mutex);
    decltype(Data->Shapes){}.swap(Data->Shapes);decltype(Data->Regions){}.swap(Data->Regions);
    if(Data->Flights.empty())decltype(Data->Flights){}.swap(Data->Flights);
    Data->Stats.ResidentBytes=0;
}
}
