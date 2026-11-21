#include "recovered_amo_orientation_range.hpp"
#include <bit>
namespace study::recovered {
namespace {
PlacementOrientation rotated(PlacementOrientation source,int32_t angle){
    source=composeOrientation(source,angle,false);
    const auto sum=std::bit_cast<int32_t>(uint32_t(source.field4)+uint32_t(source.field8));
    if(sum>36001000)source.field8=std::bit_cast<int32_t>(uint32_t(36000500)-uint32_t(source.field4));
    return source;
}
}
AmoTilingOrientationRange amoTilingOrientationRange(const AmoTilingModuleOrientations &catalog,
    PlacementOrientation first,PlacementOrientation second,bool symmetryX,bool symmetryY){
    if(!catalog)throw std::invalid_argument("Amo rotation range requires its actual module catalog");
    for(const auto step:{4,45,90,180}){
        bool complete=true;
        for(int32_t angle=step;angle<=180;angle+=step){
            const auto modules=catalog();bool accepted=false;
            const auto a=rotated(first,angle*100000),b=rotated(second,angle*100000);
            for(const auto &module:modules){const std::array one{module};
                if(equivalentOrientation(one,a,symmetryX,symmetryY).found&&equivalentOrientation(one,b,symmetryX,symmetryY).found){accepted=true;break;}}
            if(!accepted){complete=false;break;}
        }
        if(complete)return {double(180-180%step),double(step)};
    }
    return {};
}
}
