#pragma once
#include "recovered_shaker_geometry.hpp"
#include <optional>
namespace study::recovered {
struct ConvolutionEntity {
    enum class Kind {vertex,straight,curve};
    Kind kind=Kind::vertex;
    double first=0,last=0;
    int32_t index=0;
    bool shortArc=true;
    ShakerPoint start{},end{};
    uintptr_t identity=0;
};
struct ConvolutionEvent {
    enum class Kind {insert,peek,remove};
    Kind kind=Kind::insert;
    double time=0;
    uint32_t id=0,phase=0;
    size_t entity=0;
    bool sideA=false;
};
struct ConvolutionPair {
    size_t a=0,b=0;
    int32_t next=-1,previous=-1,record=-1;
    uint64_t identity=0; // native address tie-break; zero uses this object's address
};
std::vector<ConvolutionEntity> convolutionPolygon(std::span<const ShakerPoint>); // 7e02d0, straight tangent adapter
void convolutionBroaden(ConvolutionEntity &,double alpha); // 7e0590
bool convolutionCompatible(const ConvolutionEntity &,const ConvolutionEntity &,bool keepOrder); // 7e0740
int convolutionEventCompare(const ConvolutionEvent &,const ConvolutionEvent &);
// Exact heap operations of 7d6a80 and 7d6550, including phase/id ordering.
void convolutionEventPush(std::vector<ConvolutionEvent> &,ConvolutionEvent);
ConvolutionEvent convolutionEventPop(std::vector<ConvolutionEvent> &);
std::vector<ConvolutionEvent> convolutionEvents(std::span<const ConvolutionEntity>,bool sideA,uint32_t &nextId);
std::vector<ConvolutionPair> convolutionSweep(std::span<const ConvolutionEntity>,std::span<const ConvolutionEntity>); // 7e0f60 + event handlers
// 7d4ae0: only vertex/straight-edge pairs emit a record; vertex/vertex
// still computes a degenerate segment and direction but returns false.
bool convolutionSegment(const ConvolutionEntity &,const ConvolutionEntity &,ShakerBoundary &);
void convolutionLinkPairs(std::span<const ConvolutionEntity>,std::span<const ConvolutionEntity>,std::vector<ConvolutionPair> &);
// Converts existing links; caller runs convolutionLinkPairs first when needed.
std::vector<ShakerBoundary> convolutionRecords(std::span<const ConvolutionEntity>,std::span<const ConvolutionEntity>,std::vector<ConvolutionPair> &,bool adjacency);
}
