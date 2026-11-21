#pragma once
#include "recovered_convolution_topology.hpp"
#include <atomic>
namespace study::recovered {
struct ConvolutionLine {
    ShakerPoint origin{},direction{},normal{};
    double offset=0;
};
struct ConvolutionCircle {
    ShakerPoint center{};
    double radius = 0;
};
ShakerPoint convolutionCircleEvaluate(const ConvolutionCircle &, double); // 7dfab0
ShakerPoint convolutionCircleTangent(const ConvolutionCircle &, double); // 7dfb30
ShakerPoint convolutionCircleNormal(const ConvolutionCircle &, double); // 7dfb80
double convolutionCircleParameter(const ConvolutionCircle &, ShakerPoint); // 7dfbe0
bool convolutionCirclePointOn(const ConvolutionCircle &, ShakerPoint, double tolerance,
                             double &parameter); // 7dfc10
std::array<double, 4> convolutionCircleBounds(const ConvolutionCircle &, double, double); // 7dfcc0
ShakerPoint convolutionCurveEvaluate(const ConvolutionCurveSegment &, double);
std::array<double, 4> convolutionCurveBounds(const ConvolutionCurveSegment &, double, double);
double convolutionCurveParameter(const ConvolutionCurveSegment &, ShakerPoint);
double convolutionNormalizeParameter(const ConvolutionCurveSegment &, double); // 7df150
double convolutionLineParameter(const ConvolutionLine &,ShakerPoint); // 7e9dc0
ShakerPoint convolutionLineEvaluate(const ConvolutionLine &,double); // 7e9f10
std::array<double,4> convolutionLineBounds(const ConvolutionLine &,double,double); // 7e9e50
std::vector<ConvolutionEntity> convolutionPolygon(const ConvolutionTopology &); // 7e02d0 with real line normals
// Shared sequence models the original global atomic segment identity. The
// caller owns its lifetime; geometry and segment instances have separate IDs.
class ConvolutionCurves {
public:
    explicit ConvolutionCurves(uint64_t initial=0):sequence_(initial){}
    ConvolutionCurveSegment create(ShakerPoint,ShakerPoint); // 7de5a0
    ConvolutionCurveSegment createArc(ShakerPoint, ShakerPoint, ShakerPoint); // 7de810
    ConvolutionCurveSegment createRadiusArc(ShakerPoint, ShakerPoint, double signedRadius); // 7de260
    ConvolutionCurveSegment copy(const ConvolutionCurveSegment &); // 7deec0
    ConvolutionCurveSegment range(const ConvolutionCurveSegment &, double, double); // 7dedf0
    ConvolutionCurveFactory edgeFactory();
    ConvolutionArcFactory arcFactory();
    uint64_t sequence()const{return sequence_.load();}
private:
    std::atomic<uint64_t> sequence_;
};
}
