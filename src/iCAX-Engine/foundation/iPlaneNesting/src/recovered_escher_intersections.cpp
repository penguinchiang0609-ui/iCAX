#include "recovered_escher_intersections.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int64_t truncate(double value){return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
int64_t roundInteger(double value){return truncate(value<0?value-.5:value+.5);}
ConvolutionIntegerBox bounds(const ConvolutionIntegerSegment &segment){return {std::min(segment[0][0],segment[1][0]),std::min(segment[0][1],segment[1][1]),std::max(segment[0][0],segment[1][0]),std::max(segment[0][1],segment[1][1])};}
bool overlaps(ConvolutionIntegerBox a,ConvolutionIntegerBox b){return a[0]<=b[2]&&b[0]<=a[2]&&a[1]<=b[3]&&b[1]<=a[3];}
}
std::vector<EscherIntegerPoint> escherPairIntersectionDirections(const ConvolutionIntegerSegment &a,const ConvolutionIntegerSegment &b) {
    const EscherIntegerPoint da{sub(a[1][0],a[0][0]),sub(a[1][1],a[0][1])},db{sub(b[1][0],b[0][0]),sub(b[1][1],b[0][1])};
    if(mul(db[1],da[0])==mul(db[0],da[1])){std::array<EscherIntegerPoint,4> points{a[0],a[1],b[0],b[1]};std::sort(points.begin(),points.end());return {points[1],points[2]};}
    const double ax=double(a[0][0]),ay=double(a[0][1]),bx=double(a[1][0]),by=double(a[1][1]);
    const double cx=double(b[0][0]),cy=double(b[0][1]),dx=double(b[1][0]),dy=double(b[1][1]);
    const double avx=ax-bx,avy=ay-by,bvx=cx-dx,bvy=cy-dy,ac=by*ax-ay*bx,bc=dy*cx-cy*dx,den=bvy*avx-avy*bvx;
    const double x=(bvx*ac-avx*bc)/den,y=(bvy*ac-avy*bc)/den;
    const double nay=double(da[1]),nax=-double(da[0]),nby=double(db[1]),nbx=-double(db[0]);
    const auto norm=[](double vx,double vy){const double ix=double(truncate(vx)),iy=double(truncate(vy));return std::sqrt(ix*ix+iy*iy);};
    const double na=norm(nax,nay),nb=norm(nbx,nby);
    return {{roundInteger(nby/nb+nay/na+x),roundInteger(nax/na+nbx/nb+y)}};
}
std::vector<ConvolutionIntegerSegment> escherIndexBoxSegments(const ConvolutionEdgeIndex &index,ConvolutionIntegerBox box) {
    std::vector<ConvolutionIntegerSegment> output;
    const auto visit=[&](auto &&self,const ConvolutionIndexNode &node)->void{for(size_t n=0;n<node.entries.size();++n){const auto &entry=node.entries[n];if(node.leaf){if(!convolutionFilterSegmentOutside(entry.edge,box))output.push_back(entry.edge);}else if(overlaps(entry.box,box))self(self,*node.children[n]);}};
    if(index.root)visit(visit,*index.root);return output;
}
std::vector<EscherIntegerPoint> escherNfpIntersectionDirections(const ConvolutionEdgeIndex &index,EscherIntegerPoint translation) {
    std::vector<EscherIntegerPoint> output;if(!index.root||index.root->entries.empty())return output;
    auto box=index.root->entries.front().box;for(const auto &entry:index.root->entries)for(size_t axis=0;axis<2;++axis){box[axis]=std::min(box[axis],entry.box[axis]);box[axis+2]=std::max(box[axis+2],entry.box[axis+2]);}
    for(size_t axis=0;axis<2;++axis){box[axis]=sub(std::max(box[axis],add(box[axis],translation[axis])),translation[axis]);box[axis+2]=sub(std::min(box[axis+2],add(box[axis+2],translation[axis])),translation[axis]);}
    for(auto translated:escherIndexBoxSegments(index,box)){
        for(auto &point:translated)for(size_t axis=0;axis<2;++axis)point[axis]=add(point[axis],translation[axis]);
        for(const auto &stationary:escherIndexBoxSegments(index,bounds(translated)))if(convolutionFilterSegmentsIntersect(stationary,translated)){
            const auto points=escherPairIntersectionDirections(stationary,translated);output.insert(output.end(),points.begin(),points.end());
        }
    }return output;
}
}
