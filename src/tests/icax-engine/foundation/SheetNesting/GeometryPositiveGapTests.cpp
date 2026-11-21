#include "NestingInternal.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace iCAX::SheetNesting::Detail
{bool ShapesConflictReference(const Shape&,Pose,const Shape&,Pose,double,double,bool);}
using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const std::string& reason)
{if(!value)throw std::runtime_error(reason);}
std::size_t cases=0;
void Differential(const Shape& a,Pose pa,const Shape& b,Pose pb,double gap=.2,double tolerance=1e-7,bool common=false)
{
    const bool expected=Detail::ShapesConflictReference(a,pa,b,pb,gap,tolerance,common);
    const bool actual=Detail::ShapesConflict(a,pa,b,pb,gap,tolerance,common);
    Require(actual==expected,"positive gap spatial filter changed original distance verdict in case "+std::to_string(cases));
    ++cases;
}
Shape Rectangle(double width,double height,int subdivisions=1)
{
    Shape shape;const std::array<Point,4> corners{{{0,0},{width,0},{width,height},{0,height}}};
    for(std::size_t side=0;side<4;++side)for(int i=0;i<subdivisions;++i)
    {
        const auto a=corners[side],b=corners[(side+1)%4];const double t=double(i)/subdivisions;
        shape.Outer.push_back({a.X+(b.X-a.X)*t,a.Y+(b.Y-a.Y)*t});
    }
    return shape;
}
std::vector<Point> Circle(double x,double y,double radius,int count)
{
    std::vector<Point> points;
    for(int i=0;i<count;++i)
    {const double angle=2*Detail::Pi*i/count;points.push_back({x+radius*std::cos(angle),y+radius*std::sin(angle)});}
    return points;
}
void ThresholdBoundaries()
{
    const double tolerance=1e-7,gap=.2,threshold=gap-tolerance,infinity=std::numeric_limits<double>::infinity();
    for(const double distance:{0.,tolerance,std::nextafter(threshold,0.),threshold,
        std::nextafter(threshold,infinity),gap,std::nextafter(gap,infinity),gap*2})
        for(const double angle:{0.,37.,90.,143.})for(const double offset:{0.,9000000.})
        {
            const auto shape=Rectangle(100,4,20);const double radians=angle*Detail::Pi/180;
            const Pose a{offset,offset,angle},b{offset-std::sin(radians)*(4+distance),offset+std::cos(radians)*(4+distance),angle};
            Differential(shape,a,shape,b,gap,tolerance,false);
            Differential(shape,a,shape,b,gap,tolerance,true);
            Differential(shape,b,shape,a,gap,tolerance,false);
        }
    auto shape=Rectangle(10,8,8);shape.CommonEdgeRings={true};
    Differential(shape,{},shape,{10,0,0},gap,tolerance,true);
    shape.CommonEdgeRings={false};Differential(shape,{},shape,{10,0,0},gap,tolerance,true);
    Shape frame=Rectangle(40,40,20);frame.Holes={Rectangle(20,20,16).Outer};
    for(auto& p:frame.Holes.front()){p.X+=10;p.Y+=10;}
    frame.CommonEdgeRings={true,false};
    const auto plug=Rectangle(5,5,8);
    for(const double x:{10.,10.+threshold,10.+gap,15.,25.+threshold,30.})
        for(const bool common:{false,true})Differential(frame,{},plug,{x,15,0},gap,tolerance,common);
    Shape perforated=Rectangle(100,100,24);perforated.CommonEdgeRings={true};
    for(int y=0;y<5;++y)for(int x=0;x<5;++x)
    {perforated.Holes.push_back(Circle(10+x*20,10+y*20,2,25));perforated.CommonEdgeRings.push_back(false);}
    Shape inner;inner.Outer=Circle(0,0,1,29);inner.CommonEdgeRings={false};
    for(const Point center:std::array<Point,3>{{{10,10},{50,50},{90,90}}})
        for(const double shift:{0.,.7,.8,.8000001,.9,1.1})for(const bool common:{false,true})
            Differential(perforated,{},inner,{center.X+shift,center.Y,0},gap,tolerance,common);
    // Analytic circles still use the unchanged original circle branch.
    Shape disc;disc.Radius=2;disc.Center={-3,1};
    for(const double shift:{0.,3.9,4.,4.+threshold,4.+gap,20.})
    {Differential(disc,{},disc,{shift,0,0},gap,tolerance,true);Differential(disc,{},plug,{shift,0,37},gap,tolerance,true);}
    // Unsupported transformed coordinate range reaches the exhaustive fallback.
    const auto longShape=Rectangle(100,4,20);
    for(const double offset:{-100000000.,100000000.})
    {Differential(longShape,{offset,offset,37},longShape,{offset-2.5,offset+3.4,37});}
}
void RandomDifferential()
{
    std::mt19937_64 random(0x2ec7f45b);std::uniform_real_distribution<double> value(0,1);
    for(int sample=0;sample<480;++sample)
    {
        Shape a,b;const int count=17+sample%57;
        for(int i=0;i<count;++i)
        {
            const double angle=2*Detail::Pi*i/count;
            const double ra=4+value(random)*2,rb=4+value(random)*2;
            a.Outer.push_back({ra*std::cos(angle),ra*std::sin(angle)});
            b.Outer.push_back({rb*std::cos(angle),rb*std::sin(angle)});
        }
        if(sample%3==0)a.Holes={Circle(0,0,1,25)};
        if(sample%4==0)b.Holes={Circle(0,0,1,33)};
        const double angle=value(random)*360;const auto boundsA=Detail::Bounds(a,{0,0,angle});
        const auto boundsB=Detail::Bounds(b,{0,0,angle});
        const double clearance=(sample%5)*.1;
        const Pose first{0,0,angle},second{boundsA.X+boundsA.Width-boundsB.X+clearance,value(random)*2-1,angle};
        Differential(a,first,b,second,.2,1e-7,sample%2==0);
        if(sample%8==0)Differential(a,{9000000,9000000,angle},b,
            {9000000+second.X,9000000+second.Y,angle},.2,1e-7,false);
    }
}
Problem OriginalQuantityFixture()
{
    std::ifstream input(std::filesystem::path{PLANE_GAP_QUANTITY_INPUT});input.imbue(std::locale::classic());
    std::string version;std::size_t partCount=0,stockCount=0;input>>version>>partCount>>stockCount;
    Require(version=="ICAX_NESTING_PROBLEM_2"&&partCount==3&&stockCount==1,"cannot read original textile-1 gap fixture");
    Problem problem;input>>problem.PartGap>>problem.EdgeMargin>>problem.Tolerance>>problem.AllowCommonEdges;
    const auto readRing=[&](std::size_t count){std::vector<Point> ring(count);for(auto& p:ring)input>>p.X>>p.Y;return ring;};
    for(std::size_t i=0;i<partCount;++i)
    {
        Part part;std::size_t angleCount=0,outerCount=0,holeCount=0;
        input>>std::quoted(part.ID)>>part.Quantity>>std::quoted(part.Material)>>part.FreeRotation>>angleCount;
        part.AllowedAnglesDegrees.resize(angleCount);for(auto& angle:part.AllowedAnglesDegrees)input>>angle;
        input>>part.Geometry.Radius>>part.Geometry.Center.X>>part.Geometry.Center.Y>>outerCount>>holeCount;
        part.Geometry.Outer=readRing(outerCount);
        for(std::size_t hole=0;hole<holeCount;++hole){std::size_t count=0;input>>count;part.Geometry.Holes.push_back(readRing(count));}
        problem.Parts.push_back(std::move(part));
    }
    Stock stock;input>>std::quoted(stock.ID)>>stock.Width>>stock.Height>>stock.Quantity>>stock.Cost>>std::quoted(stock.Material);
    problem.Stocks.push_back(std::move(stock));
    const std::array<std::size_t,3> counts{44,88,88},vertices{70,37,26};
    for(std::size_t i=0;i<3;++i)Require(problem.Parts[i].Quantity==counts[i]&&problem.Parts[i].Geometry.Outer.size()==vertices[i]&&
        problem.Parts[i].FreeRotation&&problem.Parts[i].AllowedAnglesDegrees.empty(),"original textile-1 source changed");
    Require(bool(input)&&stock.Width==13800&&stock.Height==1000000&&problem.PartGap==.2&&problem.EdgeMargin==.5&&
        problem.AllowCommonEdges,"original textile-1 stock or clearance changed");
    return problem;
}
void OriginalGapProfile()
{
    const auto problem=OriginalQuantityFixture();
    struct Query {std::size_t Part;Pose A,B;};std::vector<Query> queries;
    for(std::size_t i=0;i<problem.Parts.size();++i)for(const double angle:{0.,37.,90.,143.})
    {
        const auto& shape=problem.Parts[i].Geometry;const auto box=Detail::Bounds(shape,{0,0,angle});
        for(const double distance:{0.,.1,.1999998,.2})
        {
            const Pose a{50000,50000,angle},b{50000+box.Width+distance,50000,angle};
            Differential(shape,a,shape,b,problem.PartGap,problem.Tolerance,problem.AllowCommonEdges);
            queries.push_back({i,a,b});
        }
    }
    const auto run=[&](bool filtered)
    {
        std::size_t blocked=0;const auto started=std::chrono::steady_clock::now();
        for(int repeat=0;repeat<20;++repeat)for(const auto& query:queries)
        {
            const auto& shape=problem.Parts[query.Part].Geometry;
            blocked+=filtered?Detail::ShapesConflict(shape,query.A,shape,query.B,problem.PartGap,problem.Tolerance,problem.AllowCommonEdges):
                Detail::ShapesConflictReference(shape,query.A,shape,query.B,problem.PartGap,problem.Tolerance,problem.AllowCommonEdges);
        }
        return std::pair(blocked,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    };
    const auto reference=run(false),filtered=run(true);
    Require(reference.first==filtered.first,"textile-1 positive-gap profile changed original verdicts");
    std::cout<<"ORIGINAL textile-1 direct ShapesConflict queries="<<queries.size()*20<<" fullScanSeconds="<<reference.second
        <<" filteredSeconds="<<filtered.second<<" blocked="<<filtered.first<<'\n';
}
}
int main()
{
    try{ThresholdBoundaries();RandomDifferential();OriginalGapProfile();std::cout<<"PASS original positive-gap differential cases="<<cases<<'\n';return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
