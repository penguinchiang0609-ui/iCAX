#include "recovered_escher_periodic_map.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t wrappedProduct(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int64_t wrappedDifference(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
double norm(EscherIntegerPoint value){return std::sqrt(double(value[1])*double(value[1])+double(value[0])*double(value[0]));}
EscherIntegerBox bounds(const ConvolutionIntegerPolygon &polygon){require(!polygon.empty()&&!polygon.front().empty(),PN2S_INVALID,"Escher periodic map requires nonempty normalized contours");
    const auto first=polygon.front().front();EscherIntegerBox result{first[0],first[1],first[0],first[1]};for(const auto &ring:polygon)for(auto point:ring){result[0]=std::min(result[0],point[0]);result[1]=std::min(result[1],point[1]);result[2]=std::max(result[2],point[0]);result[3]=std::max(result[3],point[1]);}return result;}
void scale(ConvolutionIntegerPolygon &polygon,double factor){for(auto &ring:polygon)for(auto &point:ring)point=escherBoundaryScaleVector(point,factor);}
}
EscherBoundaryTiledPolygon escherBoundaryPeriodicMap(EscherIntegerPoint step,EscherIntegerPoint horizontal,const ConvolutionIntegerPolygon &source){
    if(source.empty())return {};
    const auto determinant=wrappedDifference(wrappedProduct(horizontal[0],step[1]),wrappedProduct(horizontal[1],step[0]));
    const double sine=double(determinant)/(norm(horizontal)*norm(step));
    require(sine!=0.&&std::isfinite(sine),PN2S_INVALID,"Escher periodic map requires independent nonzero basis vectors");
    horizontal=escherBoundaryScaleVector(horizontal,sine);step=escherBoundaryScaleVector(step,sine);
    auto scaled=source;scale(scaled,sine);auto orthogonal=escherBoundaryMapBasisTransform(scaled,horizontal,step);
    for(auto &ring:orthogonal)convolutionNormalizeIntegerRing(ring);convolutionRemoveEmptyIntegerRings(orthogonal);
    size_t halvings=0;
    for(;;){const auto box=bounds(orthogonal);if(box[0]>=-0x80000000LL&&box[1]>=-0x80000000LL&&box[2]<=0x80000000LL&&box[3]<=0x80000000LL)break;
        require(halvings<64,PN2S_INVALID,"Escher periodic map coordinate reduction did not converge");++halvings;scale(orthogonal,.5);horizontal=escherBoundaryScaleVector(horizontal,.5);step=escherBoundaryScaleVector(step,.5);}
    const auto width=int64_t(norm(horizontal)),height=int64_t(norm(step));
    auto result=escherBoundaryTilePolygon(orthogonal,width,height);
    for(size_t n=0;n<halvings;++n){for(auto &polygon:result.partials)scale(polygon,2.);horizontal=escherBoundaryScaleVector(horizontal,2.);step=escherBoundaryScaleVector(step,2.);}
    for(auto &polygon:result.partials){escherBoundaryMapBasisRestore(polygon,horizontal,step);scale(polygon,1./sine);}return result;
}
}
