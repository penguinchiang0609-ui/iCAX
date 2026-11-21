#pragma once
#include "recovered_fuzzy_polygon_topology.hpp"
#include "recovered_fuzzy_polygon_vertex.hpp"
namespace study::recovered {
struct FuzzyTopologyContact {
    int32_t region=-1,relative=-1;
    bool direct=false;
    ConvolutionIntegerSegment segment{};
    bool operator==(const FuzzyTopologyContact &)const=default;
};
struct FuzzyRangeContactLocation {
    int32_t range=-1,offset=-1;
    bool present=false;
};
struct FuzzySegmentContactQuery {
    ConvolutionIntegerSegment segment{};
    int32_t region=-1;
};
struct FuzzyTopologyContactLocations {
    std::vector<FuzzyTopologyContact> direct;
    std::vector<FuzzyTopologySummitLocation> sharedVertices;
};
EscherIntegerPoint fuzzyGeometryRangePoint(const FuzzyRegionGeometry &,int32_t range,int32_t offset); //84efa0
ConvolutionIntegerSegment fuzzyGeometryPrecedingEdge(const FuzzyRegionGeometry &,int32_t range,int32_t offset); //84eab0
std::vector<FuzzyRangeContactLocation> fuzzyDirectContactLocations(const FuzzyTopology &); //843210
std::vector<FuzzyRangeContactLocation> fuzzyVertexContactLocations(const FuzzyTopology &,std::span<const int32_t>); //843d50
std::vector<FuzzyTopologySummitLocation> fuzzySegmentContactLocations(const FuzzyTopology &,
    std::span<const FuzzySegmentContactQuery>); //845430, ascending query region order
FuzzyTopologyContactLocations fuzzyTopologyContactLocations(const FuzzyTopology &); //846410
std::vector<FuzzyTopologyContact> fuzzyTopologyContacts(const FuzzyTopology &,
    const FuzzyTopologySummits &); //83ef50, original hash/group and repeated-contact order
}
