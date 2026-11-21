#include "SheetNestingNfp.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
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
    // Original ESICUP Gardeyn0 P004/P000 rings and the frozen flat precision-8
    // NFP boundary. DAT SHA256 2d3900d6da324f8b863d5bf32dcc2859fae4443507501be4987ce45319a1f96d.
    Shape LayeredFixedSource()
    {
        Shape shape;shape.Outer={
            {158,6148},{147,6144},{143,6133},{110,5602},
            {95,5361},{81,5118},{68,4872},{57,4627},
            {40,4366},{25,4102},{12,3822},{0,3538},
            {0,2610},{12,2328},{25,2047},{40,1783},
            {57,1521},{68,1276},{81,1030},{95,787},
            {110,546},{143,15},{147,4},{158,0},
            {8735,392},{8746,396},{8750,407},{8755,508},
            {8759,544},{8766,579},{8774,614},{8784,645},
            {8794,670},{8808,703},{8825,735},{8844,766},
            {8865,796},{8888,825},{8913,853},{8939,879},
            {8967,904},{8996,926},{9026,947},{9059,968},
            {9093,987},{9128,1005},{9163,1021},{9199,1035},
            {9225,1045},{9262,1057},{9300,1067},{9361,1081},
            {9422,1093},{9484,1104},{9545,1113},{9607,1121},
            {9670,1126},{9732,1130},{9795,1133},{9925,1134},
            {10056,1133},{10187,1131},{10318,1126},{10438,1120},
            {10556,1111},{10675,1101},{10785,1089},{10853,1072},
            {10864,1076},{10868,1087},{11140,2198},{11136,2209},
            {11125,2213},{11061,2228},{11026,2244},{11012,2255},
            {10995,2270},{10980,2287},{10967,2305},{10945,2342},
            {10925,2381},{10907,2420},{10897,2450},{10878,2512},
            {10862,2576},{10849,2640},{10838,2705},{10828,2780},
            {10820,2855},{10816,2930},{10813,3005},{10813,3074},
            {10813,3143},{10816,3218},{10820,3293},{10828,3368},
            {10838,3443},{10849,3508},{10862,3572},{10878,3636},
            {10897,3698},{10907,3728},{10925,3767},{10945,3806},
            {10967,3843},{10980,3861},{10995,3878},{11012,3893},
            {11026,3904},{11061,3920},{11125,3935},{11136,3939},
            {11140,3950},{10868,5061},{10864,5072},{10853,5076},
            {10785,5059},{10675,5047},{10556,5037},{10438,5028},
            {10318,5022},{10187,5017},{10056,5015},{9925,5014},
            {9795,5015},{9732,5018},{9670,5022},{9607,5027},
            {9545,5035},{9484,5044},{9422,5055},{9361,5067},
            {9300,5081},{9262,5091},{9225,5103},{9199,5113},
            {9163,5127},{9128,5143},{9093,5161},{9059,5180},
            {9026,5201},{8996,5222},{8967,5244},{8939,5269},
            {8913,5295},{8888,5323},{8865,5352},{8844,5382},
            {8825,5413},{8808,5445},{8794,5478},{8784,5503},
            {8774,5534},{8766,5569},{8759,5604},{8755,5640},
            {8750,5741},{8746,5752},{8735,5756},{158,6148}
        };return shape;
    }
    Shape LayeredMovingSource()
    {
        Shape shape;shape.Outer={
            {630,4544},{66,4351},{54,4347},{50,4336},
            {50,2334},{16,2334},{4,2330},{0,2319},
            {0,2219},{4,2208},{16,2204},{50,2204},
            {50,202},{54,191},{66,187},{624,0},
            {635,4},{672,94},{694,139},{717,185},
            {742,230},{770,275},{799,319},{830,362},
            {863,405},{897,446},{933,485},{970,523},
            {1008,560},{1048,594},{1089,626},{1115,645},
            {1134,614},{1145,610},{1156,614},{1241,666},
            {1245,677},{1241,688},{1224,716},{1301,766},
            {1322,734},{1333,730},{1344,734},{1428,788},
            {1432,799},{1428,810},{1410,838},{1430,852},
            {1498,901},{1563,950},{1625,998},{1684,1045},
            {1740,1092},{1792,1139},{1842,1185},{1890,1231},
            {1934,1277},{1976,1323},{2015,1368},{2052,1413},
            {2086,1458},{2118,1503},{2147,1548},{2174,1593},
            {2199,1638},{2222,1682},{2243,1727},{2261,1772},
            {2281,1826},{2297,1873},{2310,1918},{2321,1963},
            {2330,2008},{2338,2053},{2344,2099},{2351,2190},
            {2352,2217},{2389,2217},{2400,2221},{2404,2232},
            {2403,2332},{2399,2343},{2388,2347},{2352,2347},
            {2352,2356},{2348,2428},{2341,2498},{2331,2566},
            {2319,2631},{2304,2694},{2286,2755},{2266,2813},
            {2261,2825},{2238,2882},{2213,2936},{2185,2989},
            {2156,3039},{2124,3088},{2090,3135},{2054,3179},
            {2016,3222},{1976,3264},{1935,3303},{1893,3341},
            {1849,3377},{1803,3412},{1756,3445},{1708,3477},
            {1659,3508},{1608,3537},{1557,3565},{1450,3619},
            {1343,3667},{1284,3691},{1296,3720},{1300,3731},
            {1296,3742},{1202,3778},{1191,3782},{1180,3778},
            {1169,3748},{1114,3781},{1060,3818},{1011,3856},
            {966,3895},{924,3937},{886,3980},{852,4025},
            {820,4072},{790,4122},{763,4175},{738,4231},
            {717,4285},{698,4341},{678,4407},{645,4529},
            {641,4540},{630,4544}
        };return shape;
    }
    NfpLookup FlatUnionReference()
    {
        auto region=std::make_shared<NfpRegion>();region->ConvexPairs=5490;region->Loops={{
            {8105,-4152},{8669,-3959},{8681,-3955},{8692,-3951},
            {8700,-3929},{8705,-3828},{8709,-3792},{8716,-3757},
            {8724,-3722},{8732.3333333283663,-3696.1666666641831},{8740,-3677},{8754,-3644},
            {8771,-3612},{8790,-3581},{8811,-3551},{8834,-3522},
            {8859,-3494},{8885,-3468},{8913,-3443},{8931.6501766741276,-3428.8515901044011},
            {8977,-3423},{9040,-3418},{9102,-3414},{9165,-3411},
            {9295,-3410},{9426,-3411},{9557,-3413},{9688,-3418},
            {9808,-3424},{9926,-3433},{10045,-3443},{10155,-3455},
            {10223,-3472},{10787,-3279},{10799,-3275},{10810,-3271},
            {10818,-3249},{11090,-2138},{11090,-386},{11090,-325.22794117033482},
            {11140,-121},{11140,-21},{11132,1},{11121,5},
            {11109,9},{11090,13.453125},{11090,1596.546875},{11109,1601},
            {11121,1605},{11132,1609},{11136,1620},{11140,1631},
            {11140,1731},{11090,1935.2279411703348},{11090,3748},{10818,4859},
            {10810,4881},{10799,4885},{10787,4889},{10229,5076},
            {10161,5059},{10051,5047},{9932,5037},{9814,5028},
            {9694,5022},{9563,5017},{9432,5015},{9301,5014},
            {9171,5015},{9108,5018},{9046,5022},{8983,5027},
            {8939.9505300298333,5032.5547703132033},{8913,5053},{8885,5078},{8859,5104},
            {8834,5132},{8811,5161},{8790,5191},{8771,5222},
            {8754,5254},{8740,5287},{8732.3333333283663,5306.1666666641831},{8724,5332},
            {8716,5367},{8709,5402},{8705,5438},{8700,5539},
            {8692,5561},{8681,5565},{8669,5569},{8111,5756},
            {-466,6148},{-488,6140},{-525,6050},{-547,6005},
            {-570,5959},{-595,5914},{-623,5869},{-652,5825},
            {-683,5782},{-716,5739},{-750,5698},{-786,5659},
            {-823,5621},{-861,5584},{-901,5550},{-920.28186523914337,5534.9507393166423},
            {-987,5538},{-1009,5530},{-1094,5478},{-1102,5456},
            {-1104.5616506636143,5414.7807120233774},{-1175,5418},{-1197,5410},{-1281,5356},
            {-1289,5334},{-1292.0138296931982,5285.5047403648496},{-1351,5243},{-1416,5194},
            {-1478,5146},{-1537,5099},{-1593,5052},{-1645,5005},
            {-1695,4959},{-1743,4913},{-1787,4867},{-1829,4821},
            {-1868,4776},{-1905,4731},{-1939,4686},{-1971,4641},
            {-2000,4596},{-2027,4551},{-2052,4506},{-2075,4462},
            {-2096,4417},{-2114,4372},{-2134,4318},{-2138,4307},
            {-2154,4260},{-2167,4215},{-2178,4170},{-2187,4125},
            {-2195,4080},{-2201,4034},{-2208,3943},{-2208.7457627132535,3931},
            {-2231,3931},{-2253,3923},{-2261,3901},{-2294,3370},
            {-2309,3129},{-2323,2886},{-2336,2640},{-2347,2395},
            {-2364,2134},{-2379,1870},{-2392,1590},{-2404,1306},
            {-2404,378},{-2403,278},{-2391,-4},{-2378,-285},
            {-2363,-549},{-2346,-811},{-2335,-1056},{-2322,-1302},
            {-2308,-1545},{-2293,-1786},{-2260,-2317},{-2252,-2339},
            {-2230,-2347},{-2209.1016949117184,-2347},{-2205,-2413},{-2198,-2483},
            {-2188,-2551},{-2176,-2616},{-2161,-2679},{-2143,-2740},
            {-2123,-2798},{-2119,-2809},{-2114,-2821},{-2091,-2878},
            {-2066,-2932},{-2038,-2985},{-2009,-3035},{-1977,-3084},
            {-1943,-3131},{-1907,-3175},{-1869,-3218},{-1829,-3260},
            {-1788,-3299},{-1746,-3337},{-1702,-3373},{-1656,-3408},
            {-1609,-3441},{-1561,-3473},{-1512,-3504},{-1461,-3533},
            {-1410,-3561},{-1303,-3615},{-1196,-3663},{-1159.3677178472281,-3677.9012673124671},
            {-1157,-3716},{-1149,-3738},{-1055,-3774},{-1033,-3782},
            {-964.28609789162874,-3778.8595255166292},{-913,-3814},{-864,-3852},{-819,-3891},
            {-777,-3933},{-739,-3976},{-705,-4021},{-673,-4068},
            {-643,-4118},{-616,-4171},{-591,-4227},{-572.09999999403954,-4275.5999999940395},
            {-555,-4326},{-535,-4392},{-502,-4514},{-494,-4536},
            {-472,-4544}
        }};
        NfpLookup reference;reference.State=NfpStatus::Ready;reference.Region=std::move(region);return reference;
    }
    bool SameRegion(const NfpRegion& first,const NfpRegion& second)
    {
        if(first.Loops.size()!=second.Loops.size())return false;
        for(std::size_t ring=0;ring<first.Loops.size();++ring)
        {
            if(first.Loops[ring].size()!=second.Loops[ring].size())return false;
            for(std::size_t point=0;point<first.Loops[ring].size();++point)
                if(first.Loops[ring][point].X!=second.Loops[ring][point].X||
                    first.Loops[ring][point].Y!=second.Loops[ring][point].Y)return false;
        }
        return true;
    }
    void LayeredUnionScope()
    {
        const auto fixed=LayeredFixedSource(),moving=LayeredMovingSource();
        const auto reference=FlatUnionReference();
        NfpCache cache;NfpBuildOptions build;build.TimeLimitSeconds=30;
        std::size_t polls=0;build.Cancel=[&]{++polls;return false;};
        const auto actual=cache.Get(fixed,0,moving,0,build);
        Require(actual.State==NfpStatus::Ready&&actual.Region&&actual.Region->ConvexPairs==5490,
            "layered union discarded an original convex pair");
        Require(actual.Transform.X==0&&actual.Transform.Y==0&&actual.Transform.AngleDegrees==0,
            "layered union changed its original translation frame");
        Shape envelope;envelope.Outer=reference.Region->Loops.front();
        const auto bounds=Detail::Bounds(envelope,{});
        const auto fixedMaterial=AxisSafety::Make(fixed,{});
        for(std::size_t y=0;y<96;++y)for(std::size_t x=0;x<96;++x)
        {
            const Point translation{bounds.X-bounds.Width*.025+bounds.Width*1.05*(double(x)+.37)/96,
                bounds.Y-bounds.Height*.025+bounds.Height*1.05*(double(y)+.61)/96};
            const bool inside=Contains(actual,translation);
            Require(inside==Contains(reference,translation),"layered union disagrees with frozen flat NFP grid");
            const Pose pose{translation.X,translation.Y,0};
            Require(inside==Detail::ShapesConflict(fixed,{},moving,pose,0,1e-7),
                "layered union grid disagrees with original native material");
            const auto nominal=AxisSafety::Pair(fixedMaterial,AxisSafety::Make(moving,pose),0,false,1e-7);
            Require(nominal!=AxisSafety::Verdict::Uncertain&&inside==(nominal==AxisSafety::Verdict::Blocked),
                "layered union grid disagrees with original nominal material");
        }
        Require(polls>4,"layered union cancellation calibration has no final boundary");
        // The last helper poll follows the complete final Boolean. The next
        // poll reports the caller state after publishing immutable geometry.
        const auto completePoll=polls-1;
        NfpCache lateCache;auto cancelled=build;std::size_t latePolls=0;
        cancelled.Cancel=[&]{return ++latePolls==completePoll;};
        const auto late=lateCache.Get(fixed,0,moving,0,cancelled);
        Require(late.State==NfpStatus::Cancelled&&!late.Region&&latePolls==completePoll&&
            lateCache.Statistics().RegionEntries==1,"complete late-cancelled union was published to its caller or discarded");
        NfpBuildOptions ample;ample.TimeLimitSeconds=30;
        const auto warm=lateCache.Get(fixed,0,moving,0,ample);
        Require(warm.State==NfpStatus::Ready&&warm.CacheHit&&SameRegion(*actual.Region,*warm.Region)&&
            lateCache.Statistics().RegionBuilds==1,"complete late-cancelled cache was changed or rebuilt");
        NfpCache partialCache;std::size_t partialPolls=0;
        cancelled.Cancel=[&]{return ++partialPolls==completePoll-1;};
        const auto partial=partialCache.Get(fixed,0,moving,0,cancelled);
        Require(partial.State==NfpStatus::Cancelled&&!partial.Region&&partialPolls==completePoll-1&&
            partialCache.Statistics().RegionEntries==0,"unfinished union layers entered the region cache");
        const auto rebuilt=partialCache.Get(fixed,0,moving,0,ample);
        Require(rebuilt.State==NfpStatus::Ready&&!rebuilt.CacheHit&&SameRegion(*actual.Region,*rebuilt.Region),
            "incomplete layers masqueraded as a complete immutable NFP");
        NfpCache vertexLimited;
        const auto tooMany=vertexLimited.Get(fixed,0,fixed,0,ample);
        Require(tooMany.State==NfpStatus::LimitReached&&!tooMany.Region&&
            vertexLimited.Statistics().RegionEntries==0,"layered union relaxed the original input-vertex resource limit");
        NfpCache byteLimited({2,1,32768});
        const auto bounded=byteLimited.Get(fixed,0,moving,0,ample);
        const auto bytes=byteLimited.Statistics();
        Require(bounded.State==NfpStatus::Ready&&bytes.ShapeEntries<=2&&bytes.RegionEntries<=1&&bytes.ResidentBytes<=32768,
            "layered union bypassed immutable-cache entry or byte limits");
        NfpCache noCache({0,0,0});std::size_t disabledPolls=0;
        cancelled.Cancel=[&]{return ++disabledPolls==completePoll;};
        const auto disabledLate=noCache.Get(fixed,0,moving,0,cancelled);
        Require(disabledLate.State==NfpStatus::Cancelled&&!disabledLate.Region&&
            noCache.Statistics().RegionEntries==0&&noCache.Statistics().ResidentBytes==0,
            "late-cancelled complete geometry bypassed the disabled-cache policy");
        Require(noCache.Get(fixed,0,moving,0,ample).State==NfpStatus::Ready&&noCache.Statistics().ResidentBytes==0,
            "layered union lost computation when immutable caching was disabled");
        std::cout<<"PASS layered union 9216-point original-material differential, complete cancellation cache and resource limits\n";
    }
    void CheckDecomposition(NfpCache& cache,const Shape& source)
    {
        const auto prepared=cache.Prepare(source);const auto q=cache.Decompose(prepared);
        Require(q.State==NfpStatus::Ready&&q.Decomposition&&!q.Decomposition->Pieces.empty(),"cached convex decomposition unavailable");
        std::vector<Shape> pieces;double area=0;
        for(const auto& ring:q.Decomposition->Pieces)
        {
            Require(ring.size()>=3,"degenerate convex decomposition piece");
            for(std::size_t i=0;i<ring.size();++i)
            {
                const auto a=ring[i],b=ring[(i+1)%ring.size()],c=ring[(i+2)%ring.size()];
                Require((b.X-a.X)*(c.Y-b.Y)-(b.Y-a.Y)*(c.X-b.X)>=0,"decomposition piece is nonconvex or clockwise");
            }
            Shape piece;for(auto p:ring)piece.Outer.push_back({p.X+q.SourceOrigin.X,p.Y+q.SourceOrigin.Y});
            area+=Detail::Area(piece);pieces.push_back(std::move(piece));
        }
        Require(std::abs(area-Detail::Area(source))<1e-10,"decomposition changed original material area");
        for(std::size_t i=0;i<pieces.size();++i)for(std::size_t j=i+1;j<pieces.size();++j)
            Require(!Detail::ShapesConflict(pieces[i],{},pieces[j],{},0,1e-7),"convex decomposition pieces overlap");
        const auto probe=RectangleShape(-.017,-.019,.034,.038);
        for(double angle:{0.,37.,90.,270.})
        {
            const Pose placed{2.31,-1.17,angle};const auto box=Detail::Bounds(source,placed);
            for(int x=0;x<15;++x)for(int y=0;y<15;++y)
            {
                const Pose pose{box.X+(x+.173)*box.Width/15,box.Y+(y+.319)*box.Height/15,0};
                const bool actual=Detail::ShapesConflict(source,placed,probe,pose,0,1e-7);
                const bool decomposed=std::any_of(pieces.begin(),pieces.end(),[&](const Shape& piece){return Detail::ShapesConflict(piece,placed,probe,pose,0,1e-7);});
                Require(actual==decomposed,"canonical-origin decomposition changed transformed original material");
            }
        }
    }
    Shape ManySides(std::size_t count,bool concave=false)
    {
        Shape shape;
        for(std::size_t i=0;i<count;++i)
        {
            const double angle=2*Detail::Pi*double(i)/double(count);
            const double radius=concave&&i%2?70.:100.;
            shape.Outer.push_back({radius*std::cos(angle),radius*std::sin(angle)});
        }
        return shape;
    }
    void HighVertexScope()
    {
        NfpCache cache;NfpBuildOptions build;build.TimeLimitSeconds=10;
        const auto moving=RectangleShape(0,0,3,2);
        for(const auto& shape:{ManySides(192),ManySides(194,true),ManySides(2048)})
        {
            const auto prepared=cache.Prepare(shape,build);
            Require(static_cast<bool>(prepared),"high-vertex original shape preparation failed");
            const auto decomposition=cache.Decompose(prepared,build);
            Require(decomposition.State==NfpStatus::Ready,"high-vertex convex decomposition failed");
            double area=0;
            for(const auto& ring:decomposition.Decomposition->Pieces)
            {Shape piece;piece.Outer=ring;area+=Detail::Area(piece);}
            Require(std::abs(area-Detail::Area(shape))<1e-8,"high-vertex decomposition changed material area");
            const auto region=cache.Get(prepared,0,cache.Prepare(moving,build),37,build);
            Require(region.State==NfpStatus::Ready&&Contains(region,{.137,.219})&&!Contains(region,{400,400}),
                "high-vertex convex or concave NFP lost original interior/exterior");
        }
        Require(!cache.Prepare(ManySides(2049),build),"noncollinear vertex scope became unbounded");
        Shape segmented;
        for(int i=0;i<513;++i)segmented.Outer.push_back({double(i),0});
        for(int i=0;i<513;++i)segmented.Outer.push_back({513,double(i)});
        for(int i=0;i<513;++i)segmented.Outer.push_back({513-double(i),513});
        for(int i=0;i<513;++i)segmented.Outer.push_back({0,513-double(i)});
        Require(Detail::MaterialRing(segmented.Outer).size()>2048&&cache.Prepare(segmented,build),
            "vertex scope was applied before exact collinear cleanup");
        NfpCache small({8,8,65536});
        Require(small.Get(ManySides(2048),0,moving,37,build).State==NfpStatus::Ready&&
            small.Statistics().ResidentBytes<=65536,"large payload bypassed the existing cache byte cap");
        std::size_t polls=0;auto cancelled=build;cancelled.Cancel=[&]{return ++polls==2;};
        const auto interrupted=cache.Prepare(ManySides(2048),cancelled);
        Require(interrupted.State==NfpStatus::Cancelled&&polls==2,"high-vertex canonicalization ignored internal cancellation");
        polls=0;
        Require(cache.Get(ManySides(2048),0,moving,37,cancelled).State==NfpStatus::Cancelled&&polls==2,
            "high-vertex raw NFP lost cancellation state");
        const auto concavePrepared=cache.Prepare(ManySides(194,true),build);
        NfpCache decompositionCancel;polls=0;cancelled.Cancel=[&]{return ++polls==3;};
        Require(decompositionCancel.Decompose(concavePrepared,cancelled).State==NfpStatus::Cancelled&&polls==3,
            "high-vertex ear clipping ignored internal cancellation");
        std::cout<<"PASS bounded high-vertex convex/concave NFP, cleanup, cache and cancellation\n";
    }
}
int main()try
{
    HighVertexScope();
    LayeredUnionScope();
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
    NfpCache representation;
    const auto duplicateCold=representation.Get(duplicate,0,b,0);
    Require(duplicateCold.State==NfpStatus::Ready&&representation.Get(a,0,b,0).CacheHit&&
        cache.Get(duplicate,0,b,0).CacheHit,"exact duplicate material lost cold or warm NFP reuse");
    const auto shortCold=representation.Get(shortEdge,0,b,0);
    Require(shortCold.State==NfpStatus::Ready&&!shortCold.CacheHit&&shortCold.Region!=duplicateCold.Region&&
        representation.Get(shortEdge,0,b,0).CacheHit&&cache.Prepare(shortEdge),
        "a distinct nonzero short edge was rejected or aliased to another source key");
    NfpCache invalidCold;
    auto selfTouch=a;selfTouch.Outer.insert(selfTouch.Outer.begin()+2,a.Outer.front());
    Shape crossing;crossing.Outer={{0,0},{4,3},{0,3},{4,0}};
    for(const auto& invalid:{selfTouch,crossing})
    {
        Require(invalidCold.Get(invalid,0,b,0).State==NfpStatus::Unsupported,"invalid cold source was accepted");
        Require(cache.Get(invalid,0,b,0).State==NfpStatus::Unsupported,"hot region cache bypassed original ring validity");
        Require(!cache.Prepare(invalid),"invalid source preparation was accepted");
    }
    NfpCache shapeOnly({256,0,1024*1024});Require(shapeOnly.Get(a,0,b,0).State==NfpStatus::Ready,"shape-only baseline failed");
    Require(shapeOnly.Get(duplicate,0,b,17).State==NfpStatus::Ready&&shapeOnly.Get(shortEdge,0,b,17).State==NfpStatus::Ready,
        "shape-only cache lost exact duplicate or distinct short-edge support");
    Shape l;l.Outer={{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}};
    Shape concave;concave.Outer={{0,0},{5,0},{5,5},{4,5},{4,1},{1,1},{1,5},{0,5}};
    Compare(cache,l,0,concave,0);Compare(cache,l,37,concave,142);Compare(cache,concave,142,l,37);Compare(cache,a,90,b,90);
    CheckDecomposition(cache,l);CheckDecomposition(cache,concave);CheckDecomposition(cache,a);
    auto offsetConcave=concave;for(auto& point:offsetConcave.Outer){point.X-=17;point.Y+=23;}
    CheckDecomposition(cache,offsetConcave);
    const auto dl=cache.Decompose(concave),dshift=cache.Decompose(offsetConcave);
    Require(dl.CacheHit&&dshift.CacheHit&&dl.Decomposition==dshift.Decomposition&&dshift.SourceOrigin.X==-17&&dshift.SourceOrigin.Y==23,
        "translated source did not share immutable canonical decomposition with its own origin");
    const auto dg=separate.Decompose(cache.Prepare(offsetConcave));
    Require(dg.State==NfpStatus::Ready,"prepared decomposition handle cannot cross cache instances");
    NfpPreparedShape absent;Require(cache.Decompose(absent).State==NfpStatus::Unsupported,"empty prepared decomposition accepted");
    Require(cache.Decompose(duplicate).State==NfpStatus::Ready&&cache.Decompose(selfTouch).State==NfpStatus::Unsupported,
        "decomposition changed exact duplicate or nonadjacent-touch semantics");
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
    Require(cache.Decompose(hole).State==NfpStatus::Unsupported,"decomposition ignored original holes");
    auto circle=b;circle.Outer.clear();circle.Radius=2;
    Require(cache.Get(circle,0,b,0).State==NfpStatus::Unsupported,"circle scope failed");
    NfpBuildOptions stop;stop.Cancel=[]{return true;};Require(cache.Get(a,0,b,0,stop).State==NfpStatus::Cancelled,"cancel should stop even a cached query");
    Require(cache.Get(pa,0,pb,0,stop).State==NfpStatus::Cancelled&&!cache.Prepare(a,stop),"prepared cancel failed");
    Require(cache.Decompose(a,stop).State==NfpStatus::Cancelled&&cache.Decompose(pa,stop).State==NfpStatus::Cancelled,"cached decomposition ignored cancellation");
    stop.Cancel={};stop.TimeLimitSeconds=0;Require(cache.Get(a,0,b,0,stop).State==NfpStatus::LimitReached,"time budget failed");
    Require(cache.Decompose(a,stop).State==NfpStatus::LimitReached,"decomposition ignored time budget");
    NfpCache empty;NfpBuildOptions pairs;pairs.MaxConvexPairs=0;
    Require(empty.Get(a,0,b,0,pairs).State==NfpStatus::LimitReached,"convex pair budget failed");
    NfpCache limited({2,2,32768});
    for(int i=0;i<12;++i)Require(limited.Get(l,0,b,i*7.).State==NfpStatus::Ready,"bounded cache query failed");
    auto stats=limited.Statistics();Require(stats.ShapeEntries<=2&&stats.RegionEntries<=2&&stats.ResidentBytes<=32768&&stats.Evictions>0,"cache bounds failed");
    NfpCache noCache({0,0,0});Require(noCache.Get(a,0,b,0).State==NfpStatus::Ready,"disabled cache must preserve computation");
    Require(noCache.Statistics().ResidentBytes==0,"disabled cache retains payload");
    Require(noCache.Decompose(l).State==NfpStatus::Ready&&noCache.Statistics().ResidentBytes==0,"disabled cache lost decomposition support");
    separate.Clear();Require(dg.Decomposition&&!dg.Decomposition->Pieces.empty(),"clearing cache invalidated caller-owned decomposition");
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

