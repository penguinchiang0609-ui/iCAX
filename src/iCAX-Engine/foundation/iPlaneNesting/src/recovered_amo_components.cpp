#include "recovered_amo_components.hpp"
namespace study::recovered {
void amoTilingAttachComponent(AmoTilingTransformedShape &parent,const AmoTilingTransformedShape &child){
    if(child.properties.flag59||child.properties.flag5a)return;
    // Copy first to preserve native append semantics when the caller supplies
    // a child sharing storage with the destination.
    const auto rings=child.geometry.rings;
    for(const auto &ring:rings){
        parent.geometry.rings.push_back(ring);
        for(const auto point:ring){auto &bounds=parent.geometry.bounds;
            if(bounds.empty){bounds.empty=false;bounds.box={point.x,point.y,point.x,point.y};}
            else{if(point.x<bounds.box.x0)bounds.box.x0=point.x;if(point.y<bounds.box.y0)bounds.box.y0=point.y;
                if(bounds.box.x1<point.x)bounds.box.x1=point.x;if(bounds.box.y1<point.y)bounds.box.y1=point.y;}}
    }
}
}
