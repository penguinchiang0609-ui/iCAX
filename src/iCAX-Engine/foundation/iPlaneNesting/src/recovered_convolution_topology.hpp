#pragma once
#include "recovered_convolution.hpp"
#include <memory>
namespace study::recovered {
struct ConvolutionLine;
struct ConvolutionCircle;
struct ConvolutionCurveSegment {
    std::array<double,2> parameters{};
    std::array<ShakerPoint,2> endpoints{};
    std::array<double,4> bounds{};
    bool boundsEmpty=false;
    int64_t source=-1;
    uint64_t identity=0;
    std::shared_ptr<const ConvolutionLine> line;
    std::shared_ptr<const ConvolutionCircle> circle;
};
struct ConvolutionVertex {
    ShakerPoint point{};
    int32_t incoming=-1,outgoing=-1;
    bool alive=true;
    // Opaque words copied by 7e2e90: +08..27 followed by +48..57.
    std::array<uint64_t,6> metadata{};
};
struct ConvolutionEdge {
    int32_t first=-1,last=-1;ConvolutionCurveSegment curve;
    // +88..a7 followed by +b8..e7; interpretation belongs to the producers.
    std::array<uint64_t,10> metadata{};
};
struct ConvolutionTopology {
    std::vector<ConvolutionVertex> vertices;
    std::vector<ConvolutionEdge> edges;
    int32_t firstEdge=-1,lastEdge=-1;
    ShakerPoint cursor{1,0};
};
// The curve factory is a separate geometric dependency. Topological endpoint
// identity must not be collapsed into the curve's evaluated endpoint values.
using ConvolutionCurveFactory=std::function<ConvolutionCurveSegment(ShakerPoint,ShakerPoint)>;
using ConvolutionArcFactory=std::function<ConvolutionCurveSegment(ShakerPoint,ShakerPoint,ShakerPoint)>;
ConvolutionTopology convolutionTopology(ShakerPoint,ShakerPoint,const ConvolutionCurveFactory &); // 7d2ec0
int32_t convolutionAppend(ConvolutionTopology &,ShakerPoint,const ConvolutionCurveFactory &); // 7d3120
bool convolutionMergeClosure(ConvolutionTopology &); // 7d38a0
bool convolutionClose(ConvolutionTopology &,const ConvolutionCurveFactory &); // 7d3790
void convolutionReverse(ConvolutionTopology &); // 7d3a30
}
