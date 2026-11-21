#include "recovered_convolution_topology.hpp"
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
int32_t vertex(ConvolutionTopology &t,ShakerPoint p){const auto id=int32_t(t.vertices.size());t.vertices.push_back({p});return id;}
int32_t edge(ConvolutionTopology &t,int32_t a,int32_t b,const ConvolutionCurveFactory &factory){
    const auto id=int32_t(t.edges.size());t.edges.push_back({a,b,factory(t.vertices.at(a).point,t.vertices.at(b).point)});
    auto &first=t.vertices.at(a),&last=t.vertices.at(b);
    if(first.outgoing>=0)t.edges.at(first.outgoing).first=-1;
    first.outgoing=id;
    if(last.incoming>=0)t.edges.at(last.incoming).last=-1;
    last.incoming=id;return id;
}
}
ConvolutionTopology convolutionTopology(ShakerPoint a,ShakerPoint b,const ConvolutionCurveFactory &factory){
    ConvolutionTopology result;const auto first=vertex(result,a),last=vertex(result,b);result.firstEdge=result.lastEdge=edge(result,first,last,factory);return result;
}
int32_t convolutionAppend(ConvolutionTopology &t,ShakerPoint point,const ConvolutionCurveFactory &factory){
    int32_t first=t.lastEdge<0?-1:t.edges.at(t.lastEdge).last;
    if(first<0||t.vertices.at(first).outgoing>=0){first=vertex(t,t.cursor);const auto last=vertex(t,point);t.firstEdge=t.lastEdge=edge(t,first,last,factory);}
    else {const auto last=vertex(t,point);t.lastEdge=edge(t,first,last,factory);}
    return t.lastEdge;
}
bool convolutionMergeClosure(ConvolutionTopology &t){
    auto &lastEdge=t.edges.at(t.lastEdge);const auto first=t.edges.at(t.firstEdge).first,last=lastEdge.last;
    if(first==last)return false;auto &a=t.vertices.at(first),&b=t.vertices.at(last);
    if(!(std::abs(a.point[0]-b.point[0])<1e-9&&std::abs(a.point[1]-b.point[1])<1e-9))return false;
    b.incoming=-1;lastEdge.last=first;if(a.incoming>=0)t.edges.at(a.incoming).last=-1;a.incoming=t.lastEdge;b.alive=false;return true;
}
bool convolutionClose(ConvolutionTopology &t,const ConvolutionCurveFactory &factory){
    const auto last=t.edges.at(t.lastEdge).last,first=t.edges.at(t.firstEdge).first;
    const auto &a=t.vertices.at(last).point,&b=t.vertices.at(first).point;
    // COMISD/JBE takes the append branch for unordered coordinates too.
    if(!(std::abs(a[0]-b[0])<1e-9&&std::abs(a[1]-b[1])<1e-9)){t.lastEdge=edge(t,last,first,factory);return true;}
    convolutionMergeClosure(t);return false;
}
void convolutionReverse(ConvolutionTopology &t){
    if(t.firstEdge<0)return;
    const auto first=t.edges.at(t.firstEdge).first,last=t.edges.at(t.lastEdge).last;
    auto current=first;
    do {
        auto &v=t.vertices.at(current);auto &e=t.edges.at(v.outgoing);const auto next=e.last;
        std::swap(e.first,e.last);std::swap(e.curve.parameters[0],e.curve.parameters[1]);std::swap(e.curve.endpoints[0],e.curve.endpoints[1]);
        std::swap(v.incoming,v.outgoing);current=next;
    }while(current!=last);
    if(current!=first)std::swap(t.vertices.at(current).incoming,t.vertices.at(current).outgoing);
    std::swap(t.firstEdge,t.lastEdge);
}
}
