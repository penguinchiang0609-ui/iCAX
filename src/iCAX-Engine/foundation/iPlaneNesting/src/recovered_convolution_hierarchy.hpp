#pragma once
#include "recovered_convolution_integer.hpp"
#include <functional>
#include <memory>
namespace study::recovered {
struct ConvolutionHierarchyRelation {int32_t parent=-1;bool contained=false;};
struct ConvolutionHierarchyForest {
    std::vector<std::vector<int32_t>> children;
    std::vector<int32_t> roots;
};
// Relations distinguish containment from same-level contact. The geometry that
// produces these relations is separate from this recovered assembly layer.
ConvolutionHierarchyForest convolutionReduceHierarchyRelations(std::span<const ConvolutionHierarchyRelation>); // 826a50/824ad0
struct ConvolutionHierarchy {
    std::shared_ptr<const ConvolutionIntegerRing> ring;
    std::vector<std::shared_ptr<ConvolutionHierarchy>> children;
    bool hole=false;
};
using ConvolutionHierarchyDiagnostic=std::function<void(const char*)>;
bool convolutionHierarchyEmpty(const ConvolutionHierarchy &); // 827970
bool convolutionHierarchySimple(const ConvolutionHierarchy &); // 824480
void convolutionCheckHierarchy(const ConvolutionHierarchy &,bool,const ConvolutionHierarchyDiagnostic & = {}); // 824500; validates, does not change orientation
ConvolutionHierarchy convolutionBuildHierarchy(const ConvolutionIntegerPolygon &,std::span<const ConvolutionHierarchyRelation>,std::span<const uint8_t>,bool,const ConvolutionHierarchyDiagnostic & = {}); // 826e90/824910
ConvolutionHierarchy convolutionCanonicalizeHierarchy(const ConvolutionHierarchy &,const ConvolutionHierarchyDiagnostic & = {}); // 827390
struct ConvolutionHierarchyFlattened {ConvolutionIntegerPolygon rings;std::vector<int32_t> roots;};
ConvolutionHierarchyFlattened convolutionFlattenHierarchy(const ConvolutionHierarchy &); // 824810, preorder with top-level ring indices
std::array<bool,2> convolutionHierarchyPairDisposition(std::span<const ConvolutionIntegerPoint>,std::span<const ConvolutionIntegerPoint>); // 85ab40, strict wrapped box dimension comparisons
int64_t convolutionIntegerArea(std::span<const ConvolutionIntegerPoint>); // 3478a0, wrapped sum and truncation
struct ConvolutionHierarchyRayHit {
    ConvolutionIntegerSegment segment{};
    ConvolutionIntegerPoint point{};
    double distanceSquared=0;
    int32_t crossing=0;
    double alignment=0;
};
void convolutionClassifyHierarchyRay(ConvolutionIntegerPoint direction,ConvolutionIntegerPoint reference,ConvolutionIntegerPoint translation,ConvolutionHierarchyRayHit &,bool); // 81e050
bool convolutionHierarchyRayIntersects(const ConvolutionIntegerSegment &ray,const ConvolutionIntegerSegment &); // 83ba30, infinite forward ray
bool convolutionHierarchyEndpointOrder(const ConvolutionHierarchyRayHit &,const ConvolutionHierarchyRayHit &,const ConvolutionHierarchyDiagnostic & = {}); // 824c00
void convolutionSortHierarchyRayHits(std::vector<ConvolutionHierarchyRayHit> &,size_t depth=SIZE_MAX); // 780d30; descending before caller reversal
int convolutionHierarchyPointSide(ConvolutionIntegerPoint,const ConvolutionIntegerSegment &); // 80a110, wrapped product comparisons
bool convolutionHierarchyContains(std::span<const ConvolutionIntegerPoint>,std::span<const ConvolutionIntegerRing>,ConvolutionIntegerPoint); // 8067b0, excludes boundaries
bool convolutionHierarchyReference(std::span<const ConvolutionIntegerPoint>,ConvolutionIntegerPoint &); // 804e30/804f50
std::array<bool,2> convolutionHierarchyRingContacts(std::span<const ConvolutionIntegerPoint>,std::span<const ConvolutionIntegerPoint>); // 825150
struct ConvolutionHierarchyGeometry {std::vector<ConvolutionHierarchyRelation> relations;std::vector<uint8_t> holes;};
ConvolutionHierarchyGeometry convolutionHierarchyRelations(const ConvolutionIntegerPolygon &,const ConvolutionHierarchyDiagnostic & = {}); // 8254f0 geometry stage
ConvolutionHierarchy convolutionHierarchyFromRings(const ConvolutionIntegerPolygon &,bool,const ConvolutionHierarchyDiagnostic & = {}); // 8254f0 through 826e90
}
