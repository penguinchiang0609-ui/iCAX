#pragma once
#include "recovered_convolution_index.hpp"
#include <map>
#include <optional>
namespace study::recovered {
int convolutionFilterPointSide(ConvolutionIntegerPoint,ConvolutionIntegerPoint,ConvolutionIntegerPoint); // 820da0
bool convolutionFilterSegmentsIntersect(const ConvolutionIntegerSegment &,const ConvolutionIntegerSegment &); // 8607f0, intersection presence
bool convolutionFilterSegmentOutside(const ConvolutionIntegerSegment &,const ConvolutionIntegerBox &); // 8205e0
struct ConvolutionFilterIndex {
    std::unique_ptr<ConvolutionIndexNode> root;
    size_t height=0,count=0;
    void build(std::span<const ConvolutionIntegerSegment>); // 85eaa0/85f110/85f970, packed capacity sixteen
    bool intersects(const ConvolutionIntegerSegment &) const; // 85d6b0/85f750, first intersecting edge
};
ConvolutionIntegerPolygon convolutionFilterTopRings(const ConvolutionHierarchy &); // 859f70, immediate rings only
struct ConvolutionFilterContext {
    std::map<const ConvolutionIntegerRing*,std::optional<ConvolutionIntegerPoint>> references;
    std::map<const ConvolutionHierarchy*,ConvolutionFilterIndex> indices;
    const std::optional<ConvolutionIntegerPoint> &reference(const ConvolutionIntegerRing &); // 85bcb0
    const ConvolutionFilterIndex &index(const ConvolutionHierarchy &); // 85be80
};
using ConvolutionContourCandidates=std::vector<const ConvolutionIntegerRing*>;
bool convolutionFilterContourPair(ConvolutionFilterContext &,const ConvolutionIntegerRing &,const ConvolutionIntegerRing &,const ConvolutionIntegerRing &,bool); // 85bb40
ConvolutionContourCandidates convolutionFilterContourCandidates(ConvolutionFilterContext &,const ConvolutionHierarchy &,const ConvolutionHierarchy &,std::span<const ConvolutionIntegerRing*const>); // 85c100
using ConvolutionFilterBox=std::array<double,4>;
struct ConvolutionFilterChildEntry {ConvolutionFilterBox box{};size_t index=0;};
struct ConvolutionFilterChildNode {bool leaf=true;std::vector<ConvolutionFilterChildEntry> entries;std::vector<std::unique_ptr<ConvolutionFilterChildNode>> children;};
struct ConvolutionFilterChildIndex {
    std::unique_ptr<ConvolutionFilterChildNode> root;size_t count=0,height=0;
    void build(std::span<const ConvolutionFilterChildEntry>); // 85dd90
    std::vector<size_t> query(const ConvolutionFilterBox &,bool contained)const; // 85f630/85f420
};
ConvolutionContourCandidates convolutionFilterHierarchy(ConvolutionFilterContext &,const ConvolutionHierarchy &,const ConvolutionHierarchy &,std::span<const ConvolutionIntegerRing*const>,bool,bool,const ConvolutionHierarchyDiagnostic & = {}); // 85c400
ConvolutionContourCandidates convolutionFilterNegativeContours(const ConvolutionHierarchy &,const ConvolutionHierarchy &,std::span<const ConvolutionIntegerRing*const>,bool,const ConvolutionHierarchyDiagnostic & = {}); // 85af20
ConvolutionIntegerPolygon convolutionFilterNegativeRings(const ConvolutionHierarchy &,const ConvolutionHierarchy &,const ConvolutionIntegerPolygon &,bool,const ConvolutionHierarchyDiagnostic & = {}); // 85ace0
struct ConvolutionPrepareContext {
    struct AddressLess {bool operator()(const std::shared_ptr<const ConvolutionIntegerRing> &a,const std::shared_ptr<const ConvolutionIntegerRing> &b)const{return std::less<const ConvolutionIntegerRing*>()(a.get(),b.get());}};
    std::map<std::shared_ptr<const ConvolutionIntegerRing>,ConvolutionIntegerBox,AddressLess> bounds;
    ConvolutionIntegerBox box(const ConvolutionHierarchy &); // 859aa0
};
std::vector<const ConvolutionHierarchy*> convolutionPrepareTerminalExteriors(const ConvolutionHierarchy &); // 85a1d0
ConvolutionIntegerRing convolutionPrepareDimensions(const ConvolutionHierarchy &,ConvolutionPrepareContext &); // 85a3e0
struct ConvolutionPreparedNode {std::shared_ptr<ConvolutionHierarchy> node;bool changed=false;};
ConvolutionPreparedNode convolutionPrepareVisit(const ConvolutionHierarchy &,std::span<const ConvolutionIntegerPoint>,ConvolutionPrepareContext &); // 859cc0
std::array<ConvolutionHierarchy,2> convolutionPrepareMinkowskiOperands(const ConvolutionHierarchy &,const ConvolutionHierarchy &); // 85a610
}
