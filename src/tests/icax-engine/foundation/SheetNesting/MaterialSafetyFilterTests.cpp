#include "NestingAxisSafety.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#ifdef AXIS_FILTER_FROZEN_HEADER
#define AxisSafety FrozenAxisSafety
#include AXIS_FILTER_FROZEN_HEADER
#undef AxisSafety
#endif

using namespace iCAX::SheetNesting;
namespace
{
    std::size_t Cases=0;
    std::array<std::size_t,3> States{};
    void Require(bool condition,const char* message)
    { if (!condition) throw std::runtime_error(message); }
    Shape Rectangle(double width=10,double height=5)
    { Shape shape;shape.Outer={{0,0},{width,0},{width,height},{0,height}};return shape; }
    Shape Shifted(Shape shape,Point origin)
    {
        for (auto& point:shape.Outer) { point.X+=origin.X;point.Y+=origin.Y; }
        for (auto& ring:shape.Holes) for (auto& point:ring) { point.X+=origin.X;point.Y+=origin.Y; }
        shape.Center.X+=origin.X;shape.Center.Y+=origin.Y;
        return shape;
    }
    Pose At(Point origin,double angle,Point world)
    {
        const auto rotated=Detail::Transform(origin,{0,0,angle});
        return {world.X-rotated.X,world.Y-rotated.Y,angle};
    }
    void SameMaterial(const AxisSafety::Geometry& a,const AxisSafety::Geometry& b);
    void Compare(const Shape& a,Pose pa,const Shape& b,Pose pb,double gap,bool common)
    {
        const auto first=AxisSafety::Make(a,pa),second=AxisSafety::Make(b,pb);
        SameMaterial(first,AxisSafety::MakeExact(a,pa));
        SameMaterial(second,AxisSafety::MakeExact(b,pb));
        const auto filtered=AxisSafety::Pair(first,second,gap,common,1e-7);
        const auto reference=AxisSafety::PairExact(first,second,gap,common,1e-7);
        Require(filtered==reference,"filtered material verdict differs from the exact expansion reference");
#ifdef AXIS_FILTER_FROZEN_HEADER
        const auto oldFirst=FrozenAxisSafety::Make(a,pa),oldSecond=FrozenAxisSafety::Make(b,pb);
        const auto frozen=FrozenAxisSafety::Pair(oldFirst,oldSecond,gap,common,1e-7);
        Require(static_cast<int>(reference)==static_cast<int>(frozen),"exact reference differs from the frozen original kernel");
#endif
        ++Cases;++States[static_cast<std::size_t>(filtered)];
    }
    void SourceInvariants()
    {
        const auto shape=Rectangle();
        const Pose sourceA{5,5,0},sourceB{15-5e-8,5,0};
        const Pose candidateA{3,4,0},candidateB{13-5e-8,4,0};
        const auto a=AxisSafety::Make(shape,sourceA),b=AxisSafety::Make(shape,sourceB);
        const auto movedA=AxisSafety::Make(shape,candidateA),movedB=AxisSafety::Make(shape,candidateB);
        bool inherited=false;
        Require(AxisSafety::Pair(movedA,movedB,0,false,1e-7)==AxisSafety::Verdict::Blocked,
            "source-invariance control unexpectedly passed nominal material safety");
        const auto verdict=AxisSafety::PairWithSource(a,movedA,b,movedB,0,false,1e-7,inherited);
        Require(verdict==AxisSafety::Verdict::Safe&&inherited,"unchanged exact source relation was not preserved");
        const auto drifted=AxisSafety::Make(shape,{candidateB.X-1e-6,candidateB.Y,0});
        Require(AxisSafety::PairWithSource(a,movedA,b,drifted,0,false,1e-7,inherited)==AxisSafety::Verdict::Blocked&&
            !inherited,"changed relative material inherited an unrelated source constraint");
#ifdef AXIS_FILTER_FROZEN_HEADER
        const auto oldA=FrozenAxisSafety::Make(shape,sourceA),oldB=FrozenAxisSafety::Make(shape,sourceB);
        const auto oldMovedA=FrozenAxisSafety::Make(shape,candidateA),oldMovedB=FrozenAxisSafety::Make(shape,candidateB);
        bool oldInherited=false;
        const auto oldVerdict=FrozenAxisSafety::PairWithSource(oldA,oldMovedA,oldB,oldMovedB,0,false,1e-7,oldInherited);
        Require(static_cast<int>(verdict)==static_cast<int>(oldVerdict)&&oldInherited,
            "v2 source inheritance differs from the frozen original kernel");
#endif
    }
    void DistanceCertificates()
    {
        const auto shape=Rectangle();
        for (const double angle:{0.,23.7,37.}) for (const double gap:{0.,.125})
        {
            Compare(shape,{0,0,angle},shape,{100,100,angle},gap,false);
            const auto first=AxisSafety::Make(shape,{0,0,angle});
            const auto second=AxisSafety::Make(shape,{100,100,angle});
            const auto radius=AxisSafety::Clearance(first,second,gap);
            if (radius.Sign()>0)
                Require(AxisSafety::SegmentDistanceClearBounds(first.Rings[0][0],first.Rings[0][1],
                    second.Rings[0][0],second.Rings[0][1],radius),
                    "positive-gap or arbitrary-angle distance certificate was not exercised");
        }
        for (const double angle:{0.,37.})
        {
            // Exercise the low-level degenerate-edge guard independently of
            // the material view, which removes exact repeated source points.
            auto first=AxisSafety::Make(shape,{0,0,angle});
            first.Rings[0].insert(first.Rings[0].begin(),first.Rings[0].front());
            const auto second=AxisSafety::Make(shape,{100,100,0});
            Require(!AxisSafety::SegmentDistanceClearBounds(first.Rings[0][0],first.Rings[0][1],
                second.Rings[0][0],second.Rings[0][1],AxisSafety::Clearance(first,second,.125)),
                "degenerate source edge bypassed the exact distance path");
            Require(AxisSafety::PairExact(first,second,.125,false,1e-7)==AxisSafety::Verdict::Blocked,
                "degenerate-edge control did not retain the original contact verdict");
            bool uncertain=false;
            try { AxisSafety::PointSegmentClear(second.Rings[0][0],first.Rings[0][0],
                first.Rings[0][1],AxisSafety::Clearance(first,second,.125)); }
            catch (const AxisSafety::ArithmeticUncertainty&) { uncertain=true; }
            Require(uncertain,"degenerate point-segment control did not exercise arithmetic uncertainty");
        }
        const auto first=AxisSafety::Make(shape,{}),second=AxisSafety::Make(shape,{100,100,0});
        const double tinyRadius=0x1p-520;
        Require(!AxisSafety::SegmentDistanceClearBounds(first.Rings[0][0],first.Rings[0][1],
            second.Rings[0][0],second.Rings[0][1],AxisSafety::Number(tinyRadius)),
            "unsupported radius scale bypassed the exact distance path");
        Compare(shape,{},shape,{100,100,0},tinyRadius,false);
        Require(AxisSafety::PairExact(first,second,tinyRadius,false,1e-7)==AxisSafety::Verdict::Uncertain,
            "tiny-radius control did not exercise arithmetic uncertainty");
        Compare(shape,{0x1p129,0,0},shape,{0x1p129,100,0},.125,false);
        auto tinyEdge=shape;tinyEdge.Outer[1]={0x1p-130,0};
        Compare(tinyEdge,{},shape,{100,100,0},.125,false);

        std::mt19937_64 random(0x1afb36e2);
        std::uniform_real_distribution<double> fraction(1.,2.);
        const auto coordinate=[&]()
        {
            const double sign=random()%2?1.:-1.;
            return AxisSafety::Number(std::ldexp(sign*fraction(random),static_cast<int>(random()%257)-129));
        };
        std::size_t certified=0;
        for (std::size_t i=0;i<1000;++i)
        {
            const AxisSafety::Vector p{coordinate(),coordinate()},q{coordinate(),coordinate()};
            const AxisSafety::Vector u{coordinate(),coordinate()},v{coordinate(),coordinate()};
            const AxisSafety::Number radius(std::ldexp(fraction(random),static_cast<int>(random()%257)-129));
            if (!AxisSafety::SegmentDistanceClearBounds(p,q,u,v,radius)) continue;
            ++certified;
            try
            {
                Require(AxisSafety::PointSegmentClear(p,u,v,radius)&&AxisSafety::PointSegmentClear(q,u,v,radius)&&
                    AxisSafety::PointSegmentClear(u,p,q,radius)&&AxisSafety::PointSegmentClear(v,p,q,radius),
                    "distance certificate differs from the four exact point-segment tests");
            }
            catch (const AxisSafety::ArithmeticUncertainty&)
            { throw std::runtime_error("distance certificate bypassed an original arithmetic uncertainty"); }
        }
        Require(certified>0,"extreme-exponent distance certificate corpus produced no certificates");
    }
    void SameMaterial(const AxisSafety::Geometry& a,const AxisSafety::Geometry& b)
    {
        Require(a.Valid==b.Valid&&a.Convex==b.Convex&&a.Radius==b.Radius&&a.Roundoff==b.Roundoff&&a.CommonEdgeRings==b.CommonEdgeRings&&
            a.Rings.size()==b.Rings.size()&&a.Hull.size()==b.Hull.size(),"cached material changed geometry metadata");
        Require(a.Envelope.X.Low==b.Envelope.X.Low&&a.Envelope.X.High==b.Envelope.X.High&&
            a.Envelope.Y.Low==b.Envelope.Y.Low&&a.Envelope.Y.High==b.Envelope.Y.High,
            "filtered geometry preparation changed conservative original world bounds");
        const auto same=[](const AxisSafety::Vector& first,const AxisSafety::Vector& second)
        {return first.X.Valid==second.X.Valid&&first.Y.Valid==second.Y.Valid&&
            first.X.Terms==second.X.Terms&&first.Y.Terms==second.Y.Terms;};
        Require(same(a.Center,b.Center),"filtered geometry preparation changed an analytic circle center");
        for(std::size_t ring=0;ring<a.Rings.size();++ring)
        {
            Require(a.Rings[ring].size()==b.Rings[ring].size(),"cached material changed an original ring");
            for(std::size_t point=0;point<a.Rings[ring].size();++point)
                Require(same(a.Rings[ring][point],b.Rings[ring][point]),"cached material changed an exact coordinate term");
        }
        for(std::size_t point=0;point<a.Hull.size();++point)
            Require(same(a.Hull[point],b.Hull[point]),"cached hull changed an exact coordinate term");
    }
    void DetailedMaterialCertificates()
    {
        std::mt19937_64 random(0x1ad52ab);
        const auto star=[&](bool hole)
        {
            Shape shape;const std::size_t count=33+random()%32;
            for(std::size_t i=0;i<count;++i)
            {
                const double angle=2*Detail::Pi*i/count,radius=5.+static_cast<double>(random()%1000)/200;
                shape.Outer.push_back({radius*std::cos(angle),radius*std::sin(angle)});
            }
            if(hole)shape.Holes={{{-1,-1},{1,-1},{1,1},{-1,1}}};
            return shape;
        };
        const std::array<double,6> angles{0,90,180,270,23.7,37};
        for(std::size_t trial=0;trial<64;++trial)
        {
            const auto a=star(trial%3==0),b=star(trial%4==0);
            const Pose pa{10,10,angles[random()%angles.size()]};
            const Pose pb{double(int(random()%41)-10),double(int(random()%41)-10),angles[random()%angles.size()]};
            const auto first=AxisSafety::Make(a,pa),second=AxisSafety::Make(b,pb);
            SameMaterial(first,AxisSafety::MakeExact(a,pa));SameMaterial(second,AxisSafety::MakeExact(b,pb));
            Require(!first.Hull.empty()&&!second.Hull.empty(),"detailed corpus did not prepare its world hulls");
            for(const auto* geometry:{&first,&second})
                for(std::size_t edge=0;edge<geometry->Hull.size();++edge)
                    for(const auto& point:geometry->Rings.front())
                        Require(AxisSafety::OrientationSign(geometry->Hull[edge],geometry->Hull[(edge+1)%geometry->Hull.size()],point,true)>=0,
                            "world hull omitted nominal material after rotation and rounding");
            const auto separated=AxisSafety::SeparatedHulls(first.Hull,second.Hull);
            const bool full=AxisSafety::SeparateOnEdges(first.Hull,first.Hull,second.Hull,true)||
                AxisSafety::SeparateOnEdges(second.Hull,first.Hull,second.Hull,true);
            Require(separated&&*separated==full,"rotating support points changed full hull separation");
            for(const double gap:{0.,.125})for(const bool common:{false,true})Compare(a,pa,b,pb,gap,common);
            for(const double angle:{0.,90.,180.,270.})
            {
                const auto prepared=AxisSafety::Make(a,{0,0,angle});
                for(const Point offset:std::array<Point,3>{{{.125,15.375},{999999.375,-999999.875},{0,0}}})
                    SameMaterial(AxisSafety::Make(a,{offset.X,offset.Y,angle}),AxisSafety::TranslateQuarterTurn(prepared,offset));
            }
            Require(!AxisSafety::TranslateQuarterTurn(AxisSafety::Make(a,{0,0,37}),{1,1}).Valid,
                "quarter-turn cache accepted an arbitrary-angle roundoff enclosure");
        }
        Shape comb;comb.Outer={{0,0},{32,0}};
        for(int x=32;x>=0;--x)comb.Outer.push_back({double(x),x%2?95.:105.});
        for(const double offset:{0.,1e-8,-1e-8})for(const bool common:{false,true})
            Compare(comb,{},comb,{33,200+offset,180},0,common);
    }
    void EdgeIndexControls()
    {
        std::vector<AxisSafety::EdgeBounds> edges(24);
        for(std::size_t i=0;i<edges.size();++i)
            edges[i]={{10.+i,11.+i},{0,1},true};
        edges[5]={};edges[6]={{100,101},{100,101},false};
        edges[7]={{1.125,2},{0,1},true};edges[12]={{1,2},{0,1},true};
        const AxisSafety::EdgeBounds query{{0,1},{0,1},true};
        const AxisSafety::EdgeIndex index(edges);
        for(const double gap:{0.,.125})
        {
            const auto found=index.Query(query,gap);
            Require(std::is_sorted(found.begin(),found.end()),"edge index changed exact predicate order");
            Require(std::find(found.begin(),found.end(),5)!=found.end()&&std::find(found.begin(),found.end(),6)!=found.end(),
                "edge index discarded unsupported or degenerate source edges");
            Require(std::find(found.begin(),found.end(),12)!=found.end(),"edge index discarded an exact boundary contact");
            Require(std::find(found.begin(),found.end(),0)==found.end(),"edge index retained a certified distant edge");
            if(gap>0)Require(std::find(found.begin(),found.end(),7)!=found.end(),"edge index discarded a nominal-gap endpoint");
        }
        const std::vector<AxisSafety::EdgeBounds> empty;
        const AxisSafety::EdgeIndex small(empty);
        Require(small.Query(query,0).empty(),"empty edge index acquired a source candidate");
    }
}
int main() try
{
    Shape triangle;triangle.Outer={{0,0},{11,0},{2,8}};
    Shape concave;concave.Outer={{0,0},{10,0},{10,2},{2,2},{2,10},{0,10}};
    Shape frame=Rectangle(20,20);frame.Holes={{{4,4},{16,4},{16,16},{4,16}}};
    Shape circle;circle.Center={2,-3};circle.Radius=2.5;
    Shape skew;skew.Outer={{0,0},{12,5},{10,9},{-2,4}};
    const std::array<Shape,6> shapes{Rectangle(),triangle,concave,frame,circle,skew};
    const std::array<Point,3> origins{{{0,0},{1000000.1,-999999.8},{-9000000,8000000}}};
    const std::array<double,6> angles{0,90,180,270,23.7,37};
    std::mt19937_64 random(0x492ddc09);
    std::uniform_real_distribution<double> offset(-12,25);
    for (std::size_t i=0;i<1800;++i)
    {
        const auto originA=origins[random()%origins.size()],originB=origins[random()%origins.size()];
        const auto a=Shifted(shapes[random()%shapes.size()],originA);
        const auto b=Shifted(shapes[random()%shapes.size()],originB);
        const auto pa=At(originA,angles[random()%angles.size()],{offset(random),offset(random)});
        const auto pb=At(originB,angles[random()%angles.size()],{offset(random),offset(random)});
        Compare(a,pa,b,pb,i%3==0?.125:0,i%2==0);
    }
    for (const auto origin:origins) for (const double gap:{0.,.125})
        for (const double contact:{10.,std::nextafter(10.,0.),std::nextafter(10.,20.),10.-1e-12,10.+1e-12})
            for (const bool common:{false,true})
                Compare(Shifted(Rectangle(),origin),At(origin,0,{0,0}),Shifted(Rectangle(),origin),
                    At(origin,0,{contact+gap,1}),gap,common);
    Compare(frame,{},Rectangle(12,12),{4,4,0},0,false);
    Compare(concave,{},concave,{10,10,180},0,true);
    Compare(circle,{},circle,{5,0,0},0,false);
    auto uncertain=Rectangle();const double tiny=std::ldexp(1.,-520);
    uncertain.Outer={{tiny,tiny},{5,tiny},{5,5},{tiny,5}};
    Compare(uncertain,{},Rectangle(),{},0,false);
    Shape tinyCenter=circle;tinyCenter.Center={tiny,tiny};
    Shape zeroCenter=circle;zeroCenter.Center={0,0};
    Compare(tinyCenter,{},zeroCenter,{},0,false);
    SourceInvariants();
    DistanceCertificates();
    DetailedMaterialCertificates();
    EdgeIndexControls();
    Require(States[0]>0&&States[1]>0&&States[2]>0,"differential corpus did not exercise all three material verdicts");
    std::cout<<"material filter differential cases="<<Cases<<" safe="<<States[0]<<" blocked="<<States[1]
        <<" uncertain="<<States[2]<<" source-invariants=v2\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr<<error.what()<<'\n';return 1;
}
