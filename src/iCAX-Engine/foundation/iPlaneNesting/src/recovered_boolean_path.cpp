#include "recovered_boolean_path.hpp"
#include <immintrin.h>
#include <stdexcept>
namespace study::recovered {
namespace {
double endpointLength(const ConvolutionCurveSegment &c){
    const auto y=_mm_sub_sd(_mm_set_sd(c.endpoints[1][1]),_mm_set_sd(c.endpoints[0][1]));
    const auto x=_mm_sub_sd(_mm_set_sd(c.endpoints[1][0]),_mm_set_sd(c.endpoints[0][0]));
    const auto square=_mm_add_sd(_mm_mul_sd(x,x),_mm_mul_sd(y,y));
    return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(),square));
}
}
ConvolutionTopology booleanGlueStraightPath(const ConvolutionTopology &source,double cutoff,
    const ConvolutionCurveFactory &factory, const ConvolutionArcFactory &arcFactory){
    if(source.firstEdge<0||source.lastEdge<0)throw std::invalid_argument("Boolean cleanup requires a path");
    const auto first=source.edges.at(source.firstEdge).first,last=source.edges.at(source.lastEdge).last;
    ConvolutionTopology result;result.cursor=source.vertices.at(first).point;auto current=source.firstEdge;size_t visited=0;
    do {
        if(++visited>source.edges.size())throw std::invalid_argument("Boolean cleanup path cycle");
        const auto &e=source.edges.at(current);
        if(cutoff==0||cutoff<endpointLength(e.curve)){
            ConvolutionCurveFactory rebuild = factory;
            if (e.curve.circle) {
                if (!arcFactory) throw std::invalid_argument("Boolean cleanup requires an arc factory");
                const auto middle = convolutionCurveEvaluate(e.curve,
                    (e.curve.parameters[1] + e.curve.parameters[0]) * .5);
                rebuild = [&, middle](ShakerPoint firstPoint, ShakerPoint lastPoint) {
                    return arcFactory(firstPoint, middle, lastPoint);
                };
            } else if (!e.curve.line) {
                throw std::invalid_argument("Boolean cleanup requires a supported curve");
            }
            const auto added=convolutionAppend(result,source.vertices.at(e.last).point,rebuild);auto &out=result.edges.at(added);
            out.metadata=e.metadata;result.vertices.at(out.first).metadata=source.vertices.at(e.first).metadata;
        }
        current=source.vertices.at(e.last).outgoing;
    }while(current>=0&&current!=source.firstEdge);
    if(result.lastEdge>=0)result.vertices.at(result.edges.at(result.lastEdge).last).metadata=source.vertices.at(last).metadata;
    if(first==last){
        // The vendor helper dereferences an absent last edge in this case.
        // Reject this invalid result explicitly instead of reproducing a crash.
        if(result.lastEdge<0)throw std::invalid_argument("Boolean cleanup removed every closed-path edge");
        convolutionMergeClosure(result);
    }
    return result;
}
void booleanCleanStraightPath(ConvolutionTopology &path,double cutoff,const ConvolutionCurveFactory &factory,
                             const ConvolutionArcFactory &arcFactory){path=booleanGlueStraightPath(path,cutoff,factory,arcFactory);}
void booleanFinishStraightParts(std::span<BooleanStraightPart> parts,double cutoff,const ConvolutionCurveFactory &factory,
                               const ConvolutionArcFactory &arcFactory){
    for(auto &part:parts){if(part.outer)booleanCleanStraightPath(*part.outer,cutoff,factory,arcFactory);for(auto &hole:part.holes){if(!hole)throw std::invalid_argument("Boolean inner path is absent");booleanCleanStraightPath(*hole,cutoff,factory,arcFactory);}}
}
}
