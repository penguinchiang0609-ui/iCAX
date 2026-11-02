#include "SheetNestingNfp.h"
#include "NestingInternal.h"
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
    Shape RectangleShape(double x,double y,double w,double h){Shape s;s.Outer={{x,y},{x+w,y},{x+w,y+h},{x,y+h}};return s;}
    bool Contains(const NfpLookup& q,Point translation)
    {
        auto p=Detail::Transform({translation.X-q.Transform.X,translation.Y-q.Transform.Y},{0,0,-q.Transform.AngleDegrees});
        bool inside=false;
        for(const auto& loop:q.Region->Loops)
            for(std::size_t i=0,j=loop.size()-1;i<loop.size();j=i++)
            {
                const auto a=loop[i],b=loop[j];
                if((a.Y>p.Y)!=(b.Y>p.Y)&&p.X<(b.X-a.X)*(p.Y-a.Y)/(b.Y-a.Y)+a.X)inside=!inside;
            }
        return inside;
    }
    void Compare(NfpCache& cache,const Shape& a,double aa,const Shape& b,double ba)
    {
        const auto q=cache.Get(a,aa,b,ba);Require(q.State==NfpStatus::Ready,"NFP was not computed");
        // Offset non-grid samples avoid edge-contact semantics in this oracle.
        for(int x=-18;x<19;++x)for(int y=-18;y<19;++y)
        {
            const Point t{x*.431+.073,y*.397+.119};
            const bool actual=Detail::ShapesConflict(a,{0,0,aa},b,{t.X,t.Y,ba},0,1e-7);
            Require(Contains(q,t)==actual,"NFP interior disagrees with original material overlap");
        }
    }
}
int main()try
{
    NfpCache cache;auto a=RectangleShape(5,-6,4,3),b=RectangleShape(-2,3,2,1);
    auto first=cache.Get(a,0,b,0);Require(first.State==NfpStatus::Ready&&!first.CacheHit,"cold NFP did not build");
    auto again=cache.Get(a,0,b,0);Require(again.CacheHit&&again.Region==first.Region,"hot NFP did not reuse immutable region");
    auto pa=cache.Prepare(a),pb=cache.Prepare(b);
    Require(static_cast<bool>(pa)&&static_cast<bool>(pb),"shape preparation failed");
    auto prepared=cache.Get(pa,0,pb,0);Require(prepared.CacheHit&&prepared.Region==first.Region,"prepared query differs from raw query");
    auto changedAfterPrepare=a;auto immutable=cache.Prepare(changedAfterPrepare);changedAfterPrepare.Outer[0].X-=1;
    Require(cache.Get(immutable,0,pb,0).Region==first.Region,"prepared source was mutated by caller changes");
    NfpCache separate;Require(separate.Get(pa,0,pb,0).State==NfpStatus::Ready,"immutable prepared handles cannot cross cache instances");
    Require(first.Region->ConvexPairs==1,"convex pair was decomposed unnecessarily");
    auto opposite=cache.Get(b,0,a,0);
    Require(opposite.CacheHit&&opposite.Region==first.Region,"reverse pair did not reuse the symmetric NFP");
    Compare(cache,b,0,a,0);
    auto common=cache.Get(a,90,b,90);Require(common.CacheHit&&common.Region==first.Region,"common rotation should reuse relative-angle NFP");
    auto shifted=a;for(auto& p:shifted.Outer){p.X+=100;p.Y-=50;}
    auto translation=cache.Get(shifted,0,b,0);Require(translation.CacheHit&&translation.Region==first.Region,"local-origin translation was not reused");
    auto reversed=a;std::reverse(reversed.Outer.begin(),reversed.Outer.end());
    auto cycle=cache.Get(reversed,0,b,0);Require(cycle.CacheHit,"winding should not change geometry cache");
    std::rotate(reversed.Outer.begin(),reversed.Outer.begin()+1,reversed.Outer.end());
    Require(cache.Get(reversed,0,b,0).CacheHit,"cyclic vertex start should not change cache");
    auto different=a;different.Outer[1].X+=.001;Require(!cache.Get(different,0,b,0).CacheHit,"geometry change reused wrong NFP");
    auto duplicate=a;duplicate.Outer.insert(duplicate.Outer.begin()+1,duplicate.Outer.front());
    auto shortEdge=a;shortEdge.Outer.insert(shortEdge.Outer.begin()+1,{a.Outer.front().X+5e-8,a.Outer.front().Y});
    NfpCache invalidCold;
    for(const auto& invalid:{duplicate,shortEdge})
    {
        Require(invalidCold.Get(invalid,0,b,0).State==NfpStatus::Unsupported,"invalid cold source was accepted");
        Require(cache.Get(invalid,0,b,0).State==NfpStatus::Unsupported,"hot region cache bypassed original ring validity");
        Require(!cache.Prepare(invalid),"invalid source preparation was accepted");
    }
    NfpCache shapeOnly({256,0,1024*1024});Require(shapeOnly.Get(a,0,b,0).State==NfpStatus::Ready,"shape-only baseline failed");
    Require(shapeOnly.Get(duplicate,0,b,17).State==NfpStatus::Unsupported&&shapeOnly.Get(shortEdge,0,b,17).State==NfpStatus::Unsupported,"shape cache bypassed original ring validity");
    Shape l;l.Outer={{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}};
    Shape concave;concave.Outer={{0,0},{5,0},{5,5},{4,5},{4,1},{1,1},{1,5},{0,5}};
    Compare(cache,l,0,concave,0);Compare(cache,l,37,concave,142);Compare(cache,concave,142,l,37);Compare(cache,a,90,b,90);
    auto notch=l;notch.Outer[2].X-=.001;
    Require(!cache.Get(notch,0,concave,0).CacheHit,"same bounds with changed notch reused wrong NFP");
    Compare(cache,RectangleShape(0,0,3,2),13,RectangleShape(0,0,1,5),81);
    auto large=RectangleShape(-1e7,-1e7,2e7,2e7);NfpCache cancellationKey;
    Require(cancellationKey.Get(large,0,b,0).State==NfpStatus::Ready,"large source NFP failed");
    auto subUlpChange=large;subUlpChange.Outer[1].X=std::nextafter(subUlpChange.Outer[1].X,0.);
    Require(!cancellationKey.Get(subUlpChange,0,b,0).CacheHit,"normalization cancellation collapsed distinct source geometry");
    auto hole=l;hole.Holes={{{.2,.2},{.8,.2},{.8,.8},{.2,.8}}};
    Require(cache.Get(hole,0,b,0).State==NfpStatus::Unsupported,"holes must not silently use exterior NFP");
    Require(!cache.Prepare(hole),"unsupported prepared shape was accepted");
    auto circle=b;circle.Outer.clear();circle.Radius=2;
    Require(cache.Get(circle,0,b,0).State==NfpStatus::Unsupported,"circle scope failed");
    NfpBuildOptions stop;stop.Cancel=[]{return true;};Require(cache.Get(a,0,b,0,stop).State==NfpStatus::Cancelled,"cancel should stop even a cached query");
    Require(cache.Get(pa,0,pb,0,stop).State==NfpStatus::Cancelled&&!cache.Prepare(a,stop),"prepared cancel failed");
    stop.Cancel={};stop.TimeLimitSeconds=0;Require(cache.Get(a,0,b,0,stop).State==NfpStatus::LimitReached,"time budget failed");
    NfpCache empty;NfpBuildOptions pairs;pairs.MaxConvexPairs=0;
    Require(empty.Get(a,0,b,0,pairs).State==NfpStatus::LimitReached,"convex pair budget failed");
    NfpCache limited({2,2,32768});
    for(int i=0;i<12;++i)Require(limited.Get(l,0,b,i*7.).State==NfpStatus::Ready,"bounded cache query failed");
    auto stats=limited.Statistics();Require(stats.ShapeEntries<=2&&stats.RegionEntries<=2&&stats.ResidentBytes<=32768&&stats.Evictions>0,"cache bounds failed");
    NfpCache noCache({0,0,0});Require(noCache.Get(a,0,b,0).State==NfpStatus::Ready,"disabled cache must preserve computation");
    Require(noCache.Statistics().ResidentBytes==0,"disabled cache retains payload");
    NfpCache concurrent;std::vector<std::future<void>> tasks;std::barrier together(8);
    for(int i=0;i<8;++i)tasks.push_back(std::async(std::launch::async,[&]
    {together.arrive_and_wait();for(int k=0;k<50;++k){auto q=concurrent.Get(l,0,concave,0);Require(q.State==NfpStatus::Ready&&Contains(q,{.173,.319}),"parallel cache produced wrong region");}}));
    for(auto& task:tasks)task.get();
    stats=concurrent.Statistics();Require(stats.RegionEntries==1&&stats.RegionHits>0&&stats.RegionBuilds==1,"parallel cold requests rebuilt the same NFP");
    NfpCache reverseConcurrent;tasks.clear();std::barrier oppositeTogether(8);
    for(int i=0;i<8;++i)tasks.push_back(std::async(std::launch::async,[&,i]
    {
        oppositeTogether.arrive_and_wait();
        for(int k=0;k<30;++k)
        {
            const auto& aa=i%2?concave:l;const auto& bb=i%2?l:concave;
            const double ar=i%2?142.:37.,br=i%2?37.:142.;
            auto q=reverseConcurrent.Get(aa,ar,bb,br);
            Require(q.State==NfpStatus::Ready&&Contains(q,{.173,.319})==Detail::ShapesConflict(aa,{0,0,ar},bb,{.173,.319,br},0,1e-7),"parallel reverse cache changed the frame");
        }
    }));
    for(auto& task:tasks)task.get();
    Require(reverseConcurrent.Statistics().RegionBuilds==1,"opposite cold queries duplicated a symmetric NFP build");
    // Hold the owner after it claims the cold pair. Waiters must observe their
    // own deadlines/cancellation and cannot abort the owner's eventual result.
    NfpCache waiting;std::promise<void> claimed,release;auto released=release.get_future().share();std::atomic<int> checks=0;
    NfpBuildOptions ownerOptions;ownerOptions.TimeLimitSeconds=10;
    ownerOptions.Cancel=[&]{if(++checks==3){claimed.set_value();released.wait();}return false;};
    auto owner=std::async(std::launch::async,[&]{return waiting.Get(l,0,concave,0,ownerOptions);});
    claimed.get_future().wait();
    NfpBuildOptions waiterCancel;int waiterChecks=0;waiterCancel.Cancel=[&]{return ++waiterChecks==3;};
    const auto cancelled=waiting.Get(l,0,concave,0,waiterCancel);
    NfpBuildOptions waiterDeadline;waiterDeadline.TimeLimitSeconds=.002;
    const auto timedOut=waiting.Get(concave,0,l,0,waiterDeadline);
    release.set_value();const auto owned=owner.get();
    Require(cancelled.State==NfpStatus::Cancelled&&timedOut.State==NfpStatus::LimitReached,"coalesced waits lost independent budgets");
    Require(owned.State==NfpStatus::Ready&&waiting.Get(l,0,concave,0).CacheHit&&waiting.Statistics().RegionBuilds==1&&waiting.Statistics().CoalescedWaits>=2,"a waiter cancelled or corrupted the owner build");
    limited.Clear();stats=limited.Statistics();Require(stats.ShapeEntries==0&&stats.RegionEntries==0&&stats.ResidentBytes==0,"cache clear failed");
    std::cout<<"NFP canonicalization, overlap, bounds, budgets and concurrency passed\n";
}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

