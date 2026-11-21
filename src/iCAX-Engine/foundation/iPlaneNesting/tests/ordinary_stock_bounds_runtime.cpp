#include "ordinary_runtime.hpp"
#include <iostream>

int main(){using namespace study;try{
    size_t cases=0,placements=0;
    for(const double height:{(96.-.001)/3.,31.9994,31.9989999999,32.}) {
        Input input;input.width=160.;input.height=height;input.pitch=1.;input.randomSeed=4;
        input.parts={{{{0,0},{30,0},{30,20},{0,20}},{},{{0,false}},4},
                     {{{0,0},{15,0},{15,12},{0,12}},{},{{0,false}},3}};
        recovered::OffsetFactoryInput factory;factory.count=1;Budget budget(5.);
        const auto output=ordinaryOffset(input,offsetProgram(input,factory),budget);
        require(output.placements.size()==7&&valid_layout(input,output),PN2S_INTERNAL,
                "A fractional-height lane must retain all seven feasible rectangles within the actual stock");
        for(const auto &record:output.placements){const auto &part=input.parts.at(record.group);
            const auto box=bounds(transform(part.polygon,part.pivot,part.poses.at(record.poseIndex)));
            require(double(record.y)/1000.+box.y1<=height+1e-7,PN2S_INTERNAL,
                    "Grid boundary rounding allowed a physical vertex beyond the stock");}
        ++cases;placements+=output.placements.size();
    }
    for(const auto values:{std::array{20.,20.-1e-6},std::array{100000.,100000.-.0003}}){
        Input input;input.width=30.;input.height=values[1];input.pitch=1.;
        input.parts={{{{0,0},{30,0},{30,values[0]},{0,values[0]}},{},{{0,false}},1}};
        Budget budget(5.);const auto output=ordinary(input,budget);
        require(output.placements.empty()&&valid_layout(input,output),PN2S_INTERNAL,
                "Sheet-relative epsilon must not admit a physically oversized part");++cases;
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"placements\":"<<placements<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
