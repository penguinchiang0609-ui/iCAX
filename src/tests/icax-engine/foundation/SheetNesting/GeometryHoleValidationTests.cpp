#include "SheetNestingPlaneStrategies.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <algorithm>
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
{
// Exhaustive original distance/containment path, without the spatial filter.
bool ShapeValidReference(const Shape&,double,std::string&);
}
using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const std::string& reason)
{if(!value)throw std::runtime_error(reason);}
std::vector<Point> Box(double x,double y,double width,double height)
{return {{x,y},{x+width,y},{x+width,y+height},{x,y+height}};}
Shape TransformShape(Shape shape,Pose pose)
{
    for(auto& point:shape.Outer)point=Detail::Transform(point,pose);
    for(auto& ring:shape.Holes)for(auto& point:ring)point=Detail::Transform(point,pose);
    return shape;
}
std::size_t differentialCases=0;
void Differential(const Shape& shape,double tolerance=1e-7)
{
    std::string actualError,referenceError;
    const bool actual=Detail::ShapeValid(shape,tolerance,actualError);
    const bool reference=Detail::ShapeValidReference(shape,tolerance,referenceError);
    Require(actual==reference&&actualError==referenceError,
        "hole topology filter changed exhaustive verdict or diagnostic in case "+std::to_string(differentialCases)+
        ": filtered="+actualError+" reference="+referenceError);
    ++differentialCases;
}
void BoundaryDifferential()
{
    const double tolerance=1e-7;
    const double infinity=std::numeric_limits<double>::infinity();
    for(const double distance:{0.,std::nextafter(tolerance,0.),tolerance,
        std::nextafter(tolerance,infinity),tolerance*2,1e-5,.25})
    {
        Shape shape;shape.Outer=Box(0,0,20,20);
        for(const bool boundary:{false,true})
        for(const double angle:{0.,37.,90.})for(const double offset:{0.,9990000.})
        {
            shape.Holes=boundary?std::vector<std::vector<Point>>{Box(distance,2,1,1)}:
                std::vector<std::vector<Point>>{Box(2,5,1,1),Box(3+distance,5,1,1)};
            auto world=TransformShape(shape,{offset,offset,angle});
            Differential(world,tolerance);
            for(auto& hole:world.Holes)std::reverse(hole.begin(),hole.end());
            Differential(world,tolerance);
        }
    }
    // Nested, touching, crossing and disjoint rings whose boxes overlap.
    Shape shape;shape.Outer=Box(-50,-50,100,100);
    for(const auto& hole:std::vector<std::vector<Point>>{
        Box(-1,-1,2,2),Box(4,-1,2,2),Box(3.9,-1,2,2),Box(5,-1,2,2),
        {{4,4},{6,4},{6,6},{4,6}},{{4,4},{5,5},{6,4},{5,3}},
        {{-7,-4},{-4,-7},{-4,-4}}})
    {
        shape.Holes={Box(-4,-4,8,8),hole};
        Differential(shape);
        Differential(TransformShape(shape,{7000000,7000000,37}));
    }
    // Existing short-edge and invalid-input behavior remains observable.
    shape.Outer={{0,0},{20,0},{20,20},{10,20},{10-1e-12,20},{0,20}};
    shape.Holes={Box(3,3,1,1),Box(8,3,1,1)};Differential(shape);
    shape.Holes[1][1]=shape.Holes[1][0];Differential(shape);
    shape.Holes={Box(3,3,1,1)};shape.CommonEdgeRings={true};Differential(shape);
    shape.CommonEdgeRings.clear();
    for(int i=0;i<30;++i)shape.Outer.push_back({double(i),double(i%3)});
    shape.Outer[17].X=std::numeric_limits<double>::quiet_NaN();Differential(shape);
    shape.Outer[17].X=infinity;Differential(shape);
    Shape circle;circle.Radius=2;Differential(circle);
    circle.Holes={Box(0,0,1,1)};Differential(circle);
    Differential(shape,-1);Differential(shape,infinity);
}
void IndexedRingDifferential()
{
    const auto circle=[](double x,double y,double radius,int vertices)
    {
        std::vector<Point> ring;
        for(int i=0;i<vertices;++i)
        {const double angle=2*Detail::Pi*i/vertices;ring.push_back({x+radius*std::cos(angle),y+radius*std::sin(angle)});}
        return ring;
    };
    // More than 16 edges exercises both tree nodes and leaf edge filtering.
    for(int sample=0;sample<64;++sample)
    {
        Shape shape;shape.Outer=circle(0,0,30,72);
        for(int hole=0;hole<2+sample%6;++hole)
        {
            const double x=-10+(hole%3)*10,y=-8+(hole/3)*8;
            shape.Holes.push_back(circle(x,y,1.+(sample%13)*.4,25+sample%40));
        }
        if(sample%4==0)shape.Holes.back()=circle(-10+2.+sample%5*1e-7,-8,1,37);
        Differential(shape);
        auto world=TransformShape(shape,{sample%2?9000000.:0.,sample%2?9000000.:0.,37});
        for(auto& hole:world.Holes)std::reverse(hole.begin(),hole.end());
        Differential(world);
    }
}
void CertifiedSeparatedNearCollinear()
{
    // The original floating Cross can falsely report a proper crossing for
    // these nearly collinear but strictly separated source edges on MSVC.
    // A strict coordinate separation certificate safely avoids that false
    // rejection; exhaustive floating predicates are not an exact oracle here.
    const Point a{-6089518.327428344,-6318469.790141279},b{-1326584.4847673392,-2191334.1321229427};
    const Point c{-1326584.4846758333,-2191334.132043652},d{-71717.19797524856,-1103977.5371157571};
    Shape shape;shape.Outer=Box(-9000000,-9000000,18000000,18000000);
    shape.Holes={{a,b,{a.X-1000,a.Y+1000}},{c,d,{d.X+1000,d.Y-1000}}};
    double firstRight=-std::numeric_limits<double>::infinity(),secondLeft=-firstRight;
    for(const auto p:shape.Holes[0])firstRight=std::max(firstRight,p.X);
    for(const auto p:shape.Holes[1])secondLeft=std::min(secondLeft,p.X);
    Require(secondLeft-firstRight>1e-5,"near-collinear fixture lost strict hole separation");
    std::string error;Require(Detail::ShapeValid(shape,1e-7,error),"strictly separated near-collinear holes falsely rejected: "+error);
    std::cout<<"PASS certified near-collinear separated holes\n";
}
void RandomDifferential()
{
    std::mt19937_64 random(0x71f5b9a3);
    std::uniform_real_distribution<double> position(-24,24),size(.01,5),rotation(0,360);
    for(int sample=0;sample<700;++sample)
    {
        Shape shape;shape.Outer=Box(-30,-30,60,60);
        for(int hole=0;hole<1+sample%9;++hole)
        {
            auto ring=Box(-.5,-.5,1,1);
            const double width=size(random),height=size(random);
            for(auto& point:ring){point.X*=width;point.Y*=height;}
            const Pose pose{position(random),position(random),rotation(random)};
            for(auto& point:ring)point=Detail::Transform(point,pose);
            shape.Holes.push_back(std::move(ring));
        }
        Differential(shape);
        if(sample%7==0)Differential(TransformShape(shape,{9990000,9990000,37}));
    }
}
Problem OriginalFixture(const char* path,std::size_t vertices,std::size_t holes,std::size_t totalVertices)
{
    std::ifstream input(std::filesystem::path{path});input.imbue(std::locale::classic());
    std::string version;std::size_t partCount=0,stockCount=0;input>>version>>partCount>>stockCount;
    Require(version=="ICAX_NESTING_PROBLEM_3"&&partCount==1&&stockCount==1,"original multi-hole input header changed");
    Problem problem;input>>problem.PartGap>>problem.EdgeMargin>>problem.Tolerance>>problem.AllowCommonEdges;
    const auto ring=[&](std::size_t count){std::vector<Point> points(count);for(auto& p:points)input>>p.X>>p.Y;return points;};
    Part part;std::size_t angleCount=0,outerCount=0,holeCount=0;
    input>>std::quoted(part.ID)>>part.Quantity>>std::quoted(part.Material)>>part.FreeRotation>>angleCount;
    part.AllowedAnglesDegrees.resize(angleCount);for(auto& angle:part.AllowedAnglesDegrees)input>>angle;
    input>>part.Geometry.Radius>>part.Geometry.Center.X>>part.Geometry.Center.Y>>outerCount>>holeCount;
    part.Geometry.Outer=ring(outerCount);
    std::size_t actualVertices=outerCount;
    for(std::size_t hole=0;hole<holeCount;++hole)
    {std::size_t count=0;input>>count;actualVertices+=count;part.Geometry.Holes.push_back(ring(count));}
    std::size_t flagCount=0;input>>flagCount;
    for(std::size_t flag=0;flag<flagCount;++flag){bool allowed=false;input>>allowed;part.Geometry.CommonEdgeRings.push_back(allowed);}
    Stock stock;input>>std::quoted(stock.ID)>>stock.Width>>stock.Height>>stock.Quantity>>stock.Cost>>std::quoted(stock.Material);
    Require(bool(input)&&part.Quantity==1&&part.FreeRotation&&angleCount==0&&outerCount==vertices&&holeCount==holes&&
        actualVertices==totalVertices&&flagCount==holes+1&&stock.Width==3000&&stock.Height==6000&&stock.Quantity==1&&
        problem.PartGap==.2&&problem.EdgeMargin==.5&&problem.AllowCommonEdges,"original multi-hole demand/geometry/stock changed");
    problem.Parts.push_back(std::move(part));problem.Stocks.push_back(std::move(stock));return problem;
}
void OriginalConstruction(const char* name,const Problem& problem)
{
    const auto started=std::chrono::steady_clock::now();
    const auto errors=Detail::CheckProblem(problem);
    const auto checkedAt=std::chrono::steady_clock::now();
    Require(errors.empty(),errors.empty()?"":errors.front());
    Options options;options.TimeLimitSeconds=15;options.MaxPlacementAttempts=8000000;options.EnableBasicCompaction=false;
    const auto result=MakePlaneEnvelopeInitialFactory()()->Run(problem,options);
    const auto constructedAt=std::chrono::steady_clock::now();
    const auto checked=Validate(problem,result);
    const auto finished=std::chrono::steady_clock::now();
    Require(checked.Valid&&checked.Complete&&checked.PlacedCount==1&&result.Layouts.size()==1,
        std::string(name)+" original multi-hole initial construction failed");
    const auto& layout=result.Layouts.front();
    const auto& placed=layout.Placements.front();
    Require(AxisSafety::Containment(AxisSafety::Make(problem.Parts.front().Geometry,placed.Transform),
        layout.Width,layout.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe,"original multi-hole exact final containment failed");
    std::cout<<"ORIGINAL "<<name<<" checkSeconds="<<std::chrono::duration<double>(checkedAt-started).count()
        <<" initialSeconds="<<std::chrono::duration<double>(constructedAt-checkedAt).count()
        <<" finalValidationSeconds="<<std::chrono::duration<double>(finished-constructedAt).count()
        <<" placed=1/1 boards=1 holes="<<problem.Parts.front().Geometry.Holes.size()<<'\n';
}
}
int main()
{
    try
    {
        BoundaryDifferential();RandomDifferential();IndexedRingDifferential();CertifiedSeparatedNearCollinear();
        std::cout<<"PASS exhaustive topology differential cases="<<differentialCases<<'\n'<<std::flush;
        OriginalConstruction("plaque344",OriginalFixture(PLANE_PLAQUE_INPUT,1650,344,34423));
        OriginalConstruction("housing199",OriginalFixture(PLANE_HOUSING_INPUT,1908,199,20435));
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
