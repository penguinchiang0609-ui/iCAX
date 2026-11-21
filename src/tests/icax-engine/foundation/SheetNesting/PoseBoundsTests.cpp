#include "NestingPoseBounds.h"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

using namespace iCAX::SheetNesting;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::uint64_t bits(double value){return std::bit_cast<std::uint64_t>(value);}
static bool samePoint(Point a,Point b){return bits(a.X)==bits(b.X)&&bits(a.Y)==bits(b.Y);}
static bool sameRing(const std::vector<Point>& a,const std::vector<Point>& b)
{return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),samePoint);}
static void unchanged(const Shape& actual,const Shape& saved)
{
    require(sameRing(actual.Outer,saved.Outer)&&bits(actual.Radius)==bits(saved.Radius)&&
        samePoint(actual.Center,saved.Center)&&actual.CommonEdgeRings==saved.CommonEdgeRings&&
        actual.Holes.size()==saved.Holes.size(),"prepared scoring bounds altered its original source shape");
    for(std::size_t i=0;i<actual.Holes.size();++i)
        require(sameRing(actual.Holes[i],saved.Holes[i]),"prepared scoring bounds altered an original inner material ring");
}
static std::size_t comparisons=0;
static void equalBounds(const Detail::PreparedPoseBounds& prepared,const Shape& shape,Pose pose)
{
    const auto actual=prepared.At(pose),expected=Detail::Bounds(shape,pose);++comparisons;
    if(bits(actual.X)!=bits(expected.X)||bits(actual.Y)!=bits(expected.Y)||
        bits(actual.Width)!=bits(expected.Width)||bits(actual.Height)!=bits(expected.Height))
    {
        std::cerr<<"bounds mismatch angle="<<pose.AngleDegrees<<" mirrored="<<pose.Mirrored
            <<" translation="<<pose.X<<","<<pose.Y<<" actual="<<std::hexfloat
            <<actual.X<<","<<actual.Y<<","<<actual.Width<<","<<actual.Height<<" expected="
            <<expected.X<<","<<expected.Y<<","<<expected.Width<<","<<expected.Height<<std::defaultfloat<<"\n";
        throw std::runtime_error("prepared scoring bounds changed a stored binary64 component; epsilon equality is forbidden");
    }
}
static std::vector<Shape> sources()
{
    const double next=std::nextafter(1.,2.),tiny=std::numeric_limits<double>::denorm_min();
    std::vector<Shape> shapes;
    Shape irregular;irregular.Outer={{-7.25,11.5},{-2.,11.5},{-2.,12.125},{4.75,12.125},
        {6.5,16.},{3.,19.25},{2.75,14.5},{-5.125,20.5},{-7.25,11.5}};
    irregular.Holes={{{-2.,13.},{-1.,13.},{-1.,14.},{-2.,14.}}};
    irregular.CommonEdgeRings={true,false};shapes.push_back(irregular);
    Shape shortEdge;shortEdge.Outer={{1.,1.},{next,1.},{next,next},{1.,next}};shapes.push_back(shortEdge);
    Shape subnormal;subnormal.Outer={{0.,0.},{tiny,0.},{tiny,tiny},{0.,tiny}};shapes.push_back(subnormal);
    Shape offset;offset.Outer={{1e16,-1e16},{1e16+2,-1e16+4},{1e16+16,-1e16+2},
        {1e16+10,-1e16+32},{1e16+4,-1e16+18}};shapes.push_back(offset);
    Shape huge;huge.Outer={{1e300,-1e300},{std::nextafter(1e300,0.),-1e300},
        {1e300,std::nextafter(-1e300,0.)},{std::nextafter(1e300,2e300),std::nextafter(-1e300,-2e300)}};
    shapes.push_back(huge);
    Shape zeros;zeros.Outer={{-0.,-0.},{0.,-0.},{0.,0.},{-0.,0.}};shapes.push_back(zeros);
    Shape many;
    for(std::size_t i=0;i<121;++i)
    {
        const double a=double(i)*2*Detail::Pi/121.,r=i%3?23.:11.;
        many.Outer.push_back({17.+r*std::cos(a),-31.+r*std::sin(a)});
    }
    shapes.push_back(std::move(many));
    Shape circle;circle.Center={-13.25,19.5};circle.Radius=3.125;
    circle.Outer={{100.,100.},{200.,100.},{100.,200.}};shapes.push_back(circle);
    Shape empty;empty.Holes={{{1.,1.},{2.,1.},{1.,2.}}};shapes.push_back(empty);
    Shape malformed;malformed.Outer={{std::numeric_limits<double>::infinity(),0.},{1.,1.},{2.,2.}};
    shapes.push_back(malformed);
    return shapes;
}
static void ExactOrientationsTranslationsAndFallbacks()
{
    const double inf=std::numeric_limits<double>::infinity();
    const std::vector<double> angles{0.,-0.,90.,180.,270.,360.,-90.,23.7,-37.25,1e-9,89.99999999999999,
        std::nextafter(90.,inf),137.125,721.25,1e100};
    for(const auto& shape:sources())
    {
        const auto saved=shape;
        for(const double angle:angles)for(const bool mirror:{false,true})
        {
            const Pose orientation{123.,-456.,angle,mirror};Detail::PreparedPoseBounds prepared(shape,orientation);
            std::vector<Point> translations{{0.,0.},{-0.,-0.},{.1,.2},{-17.125,31.25},
                {1e16,-1e16},{-1e16,1e16},{0x1p54,0x1p54},{-1e300,1e300},
                {std::numeric_limits<double>::max(),-std::numeric_limits<double>::max()}};
            // Exercise translations immediately around exact cancellation and
            // rounding ties for every actual rotated source vertex.
            for(const auto vertex:shape.Outer)
            {
                const auto rotated=Detail::Transform(vertex,{0.,0.,angle,mirror});
                if(!std::isfinite(rotated.X)||!std::isfinite(rotated.Y))continue;
                const double x=-rotated.X,y=-rotated.Y;
                translations.push_back({x,y});
                translations.push_back({std::nextafter(x,-inf),std::nextafter(y,inf)});
                translations.push_back({std::nextafter(x,inf),std::nextafter(y,-inf)});
            }
            for(const auto at:translations)equalBounds(prepared,shape,{at.X,at.Y,angle,mirror});
            // A different stored angle/parity must use the original Bounds,
            // including numerically close or equivalent normalized angles.
            for(const double changed:{angle+90.,angle+360.,std::nextafter(angle,inf)})
                equalBounds(prepared,shape,{1e16,-1e16,changed,mirror});
            equalBounds(prepared,shape,{-17.,31.,angle,!mirror});
        }
        unchanged(shape,saved);
    }
    // Width is recomputed from transformed extrema. Large translations can
    // round distinct source endpoints to the same actual world coordinate.
    Shape unit;unit.Outer={{0.,0.},{1.,0.},{1.,1.},{0.,1.}};Detail::PreparedPoseBounds prepared(unit,{});
    const auto collapsed=prepared.At({0x1p54,0x1p54,0});
    require(collapsed.Width==0&&collapsed.Height==0,"prepared scoring bounds retained a cached untranslated width");
    equalBounds(prepared,unit,{0x1p54,0x1p54,0});
}
static void DeterministicDifferentialCases()
{
    std::mt19937_64 random(0x53e71ab4);
    const auto unit=[&]{return double(random()>>11)*0x1p-53;};
    for(std::size_t sample=0;sample<160;++sample)
    {
        Shape shape;const double scale=std::ldexp(1.,int(sample%41)-20);
        const double x=std::ldexp(unit()-.5,int(sample%95)),y=std::ldexp(unit()-.5,int((sample*7)%95));
        for(std::size_t vertex=0;vertex<17+sample%31;++vertex)
            shape.Outer.push_back({x+scale*(unit()-.5),y+scale*(unit()-.5)});
        const auto saved=shape;const double angle=(unit()-.5)*2000.;const bool mirror=sample%2!=0;
        Detail::PreparedPoseBounds prepared(shape,{0,0,angle,mirror});
        for(std::size_t translation=0;translation<40;++translation)
        {
            const double px=translation%2?-x:std::ldexp(unit()-.5,int(translation%81));
            const double py=translation%3?-y:std::ldexp(unit()-.5,int((translation*13)%81));
            equalBounds(prepared,shape,{px,py,angle,mirror});
        }
        unchanged(shape,saved);
    }
}
static void UnsupportedArithmeticAlwaysUsesOriginalBounds()
{
    const int original=std::fegetround();
    struct Restore{int Mode;~Restore(){std::fesetround(Mode);}}restore{original};
    require(std::fesetround(FE_TONEAREST)==0,"could not establish supported rounding for prepared bounds");
    const auto shape=sources().front();const auto saved=shape;const Pose orientation{0,0,23.7,true};
    Detail::PreparedPoseBounds prepared(shape,orientation);
    for(const int mode:{FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO})
    {
        require(std::fesetround(mode)==0,"could not exercise unsupported bounds rounding");
        equalBounds(prepared,shape,{-17.,31.,23.7,true});
        Detail::PreparedPoseBounds unavailable(shape,orientation);
        require(std::fesetround(FE_TONEAREST)==0,"could not restore bounds rounding");
        equalBounds(unavailable,shape,{1e16,-1e16,23.7,true});
    }
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
    const unsigned control=_mm_getcsr();
    struct RestoreSse{unsigned Control;~RestoreSse(){_mm_setcsr(Control);}}restoreSse{control};
    for(const unsigned flags:{1u<<15,1u<<6,(1u<<15)|(1u<<6)})
    {
        _mm_setcsr(control|flags);
        equalBounds(prepared,shape,{-.1,.2,23.7,true});
        Detail::PreparedPoseBounds unavailable(shape,orientation);
        _mm_setcsr(control);
        equalBounds(unavailable,shape,{-17.,31.,23.7,true});
    }
#endif
    unchanged(shape,saved);
}
int main()try
{
    ExactOrientationsTranslationsAndFallbacks();
    DeterministicDifferentialCases();
    UnsupportedArithmeticAlwaysUsesOriginalBounds();
    std::cout<<"PASS "<<comparisons<<" exact binary64 prepared/original bounds comparisons, unchanged sources and arithmetic/orientation fallbacks\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
