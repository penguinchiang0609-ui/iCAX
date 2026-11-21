#include "recovered_amo_convolution.hpp"
#include "recovered_amo_raster.hpp"
namespace study::recovered {
std::optional<std::vector<BooleanStraightPart>> amoFinishConvolution(
    std::span<const ConvolutionTopology> paths,ConvolutionCurves &curves){
    std::vector<std::vector<Point>> contours;double low=0,high=0;bool empty=true;
    for(const auto &path:paths){
        std::vector<Point> points;auto current=path.firstEdge;
        if(current<0)throw std::invalid_argument("Amo convolution finish requires nonempty paths");
        for(;;){
            const auto &edge=path.edges.at(current);
            if(edge.first>=0){const auto point=path.vertices.at(edge.first).point;points.push_back({point[0],point[1]});}
            if(current==path.lastEdge)break;
            current=path.vertices.at(edge.last).outgoing;
        }
        const auto last=path.edges.at(path.lastEdge).last;
        if(last>=0){const auto point=path.vertices.at(last).point;points.push_back({point[0],point[1]});}
        points.pop_back();
        for(const auto point:points){if(empty){low=high=point.x;empty=false;}else {if(point.x<low)low=point.x;if(high<point.x)high=point.x;}}
        contours.push_back(std::move(points));
    }
    if(empty)throw std::invalid_argument("Amo convolution finish requires source vertices");
    const double pitch=(high-low)/100.;
    const auto raster=amoRasterStrips(contours,{pitch*.5+low,0.},low,pitch,pitch*.001,pitch*.000001,0.);
    ConvolutionTopology graph;std::vector<BooleanEdgePair> anchors;
    const auto factory=curves.edgeFactory();
    for(int32_t bin=0;bin<raster.binCount;++bin){
        const double first=double(bin)*pitch+low,last=double(bin+1)*pitch+low;
        for(auto n=raster.indices.at(size_t(bin));n<raster.indices.at(size_t(bin)+1);++n){
            const auto ys=raster.intervals.at(size_t(n));
            const std::array<ShakerPoint,4> rectangle{{{first,ys[0]},{last,ys[0]},{last,ys[1]},{first,ys[1]}}};
            auto path=convolutionTopology(rectangle[0],rectangle[1],factory);
            convolutionAppend(path,rectangle[2],factory);convolutionAppend(path,rectangle[3],factory);convolutionAppend(path,rectangle[0],factory);
            const auto edgeOffset=int32_t(graph.edges.size()),vertexOffset=int32_t(graph.vertices.size());
            anchors.push_back({path.firstEdge+edgeOffset,path.lastEdge+edgeOffset});
            for(auto vertex:path.vertices){if(vertex.incoming>=0)vertex.incoming+=edgeOffset;if(vertex.outgoing>=0)vertex.outgoing+=edgeOffset;graph.vertices.push_back(vertex);}
            for(auto edge:path.edges){if(edge.first>=0)edge.first+=vertexOffset;if(edge.last>=0)edge.last+=vertexOffset;graph.edges.push_back(std::move(edge));}
        }
    }
    return booleanArrangeStraightPaths(std::move(graph),anchors,pitch*.000001,curves,0,true,false);
}
std::optional<std::vector<BooleanStraightPart>> amoConvolveStraightParts(
    std::span<const BooleanStraightPart> first,std::span<const BooleanStraightPart> second,ConvolutionCurves &curves){
    std::array<std::vector<const ConvolutionTopology*>,2> outer,holes;
    for(size_t side=0;side<2;++side)for(const auto &part:side?second:first){
        if(part.outer)outer[side].push_back(part.outer.get());
        for(const auto &hole:part.holes)holes[side].push_back(hole.get());
    }
    std::vector<ConvolutionTopology> paths;
    const auto append=[&](const ConvolutionTopology &a,const ConvolutionTopology &b){auto produced=amoConvolveStraightPaths(a,b,curves);for(auto &path:produced)paths.push_back(std::move(path));};
    for(const auto a:outer[0]){for(const auto b:outer[1])append(*a,*b);for(const auto b:holes[1])append(*a,*b);}
    for(const auto a:holes[0])for(const auto b:outer[1])append(*a,*b);
    return amoFinishConvolution(paths,curves);
}
}
