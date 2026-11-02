#include "NestingSpatialIndex.h"
#include "NestingInternal.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace iCAX::SheetNesting;
using namespace iCAX::SheetNesting::Detail;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(const SpatialBounds& a,const SpatialBounds& b,double gap)
{
    const long double dx=std::max({0.L,static_cast<long double>(a.Left)-b.Right,static_cast<long double>(b.Left)-a.Right});
    const long double dy=std::max({0.L,static_cast<long double>(a.Bottom)-b.Top,static_cast<long double>(b.Bottom)-a.Top});
    return dx<=gap&&dy<=gap;
}
Shape Box(double w,double h){Shape s;s.Outer={{0,0},{w,0},{w,h},{0,h}};return s;}
}
int main()
{
 try {
    std::size_t checks=0;std::mt19937_64 rng(0x22506000);
    std::uniform_real_distribution<double> origin(-10000,10000),size(0,900);
    for(int seed=0;seed<12;++seed)
    {
        std::vector<SpatialBounds> boxes;
        for(int i=0;i<220;++i){double x=origin(rng),y=origin(rng);boxes.push_back({x,y,x+size(rng),y+size(rng)});}
        boxes.push_back(boxes[0]);boxes.push_back({0,0,0,0});
        SpatialIndex index(boxes);
        for(double gap:{0.,1e-7,3.,1000.})for(std::size_t i=0;i<boxes.size();++i)
        {
            const auto found=index.Query(boxes[i],gap,i);
            Require(std::is_sorted(found.begin(),found.end())&&std::adjacent_find(found.begin(),found.end())==found.end(),"unstable or duplicate candidates");
            for(std::size_t j=0;j<i;++j)if(Near(boxes[i],boxes[j],gap))
                Require(std::binary_search(found.begin(),found.end(),j),"spatial index omitted a geometrically possible conflict");
            for(auto j:found)Require(j<i,"exclusive upper bound violated");++checks;
        }
    }
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    std::vector<SpatialBounds> extreme={{-1e308,-1e308,-9e307,-9e307},{9e307,9e307,1e308,1e308},{nan,0,0,0},{2,2,1,1}};
    const SpatialIndex special(extreme);
    Require(special.Query({-1e308,-1e308,1e308,1e308},1e308).size()==4,"overflow expansion excluded candidates");
    Require(special.Query({nan,0,0,0}).size()==4,"nonfinite query must conservatively enumerate");
    Require(special.Query({0,0,0,0},0,0).empty(),"empty prefix query was not empty");
    const auto unindexed=special.Query({0,0,0,0});Require(std::find(unindexed.begin(),unindexed.end(),2)!=unindexed.end()&&std::find(unindexed.begin(),unindexed.end(),3)!=unindexed.end(),"unindexable bounds omitted");checks+=4;
    for(int seed=0;seed<3;++seed)
    {
        std::vector<Shape> shapes;std::vector<Pose> poses;std::vector<SpatialBounds> boxes;
        for(int i=0;i<100;++i)
        {
            auto shape=Box(10+(rng()%30),10+(rng()%30));
            if(i%4==0){shape={};shape.Radius=3+(rng()%15);shape.Center={-7,11};}
            if(i%4==1){shape.Outer={{0,0},{30,0},{30,8},{10,8},{10,30},{0,30}};}
            if(i%4==2){shape=Box(30,30);shape.Holes={{{5,5},{5,25},{25,25},{25,5}}};}
            shapes.push_back(shape);poses.push_back({static_cast<double>(rng()%200)-100,static_cast<double>(rng()%200)-100,std::vector<double>{0,37,90,180}[rng()%4]});
            boxes.push_back(SpatialBounds::From(Bounds(shapes.back(),poses.back())));
        }
        const SpatialIndex index(boxes);
        for(double gap:{0.,3.,10.})for(std::size_t i=0;i<shapes.size();++i)
        {
            const auto found=index.Query(boxes[i],gap,i);
            for(std::size_t j=0;j<i;++j)if(ShapesConflict(shapes[i],poses[i],shapes[j],poses[j],gap,1e-6))
                Require(std::binary_search(found.begin(),found.end(),j),"narrow-phase conflict was pruned");
            ++checks;
        }
    }
    std::cout<<"SpatialIndexTests: "<<checks<<" safe-query checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
