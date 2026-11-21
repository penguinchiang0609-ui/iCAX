#pragma once
#include "recovered_escher_boundary_tiles.hpp"
namespace study::recovered {
struct EscherBoundaryBorderEndpoint {
    ConvolutionIntegerSegment edge{};
    int32_t endpoint=0,padding=0;
    bool operator==(const EscherBoundaryBorderEndpoint &)const=default;
};
static_assert(sizeof(EscherBoundaryBorderEndpoint)==40);
bool escherBoundaryBorderEndpointLess(const EscherBoundaryBorderEndpoint &,
    const EscherBoundaryBorderEndpoint &,int32_t axis,int32_t side); //7b1a30/7a5ea0
void escherBoundarySortBorderEndpoints(std::vector<EscherBoundaryBorderEndpoint> &,int32_t axis,int32_t side);
std::vector<ConvolutionIntegerSegment> escherBoundaryCloseInteriorTile(std::span<const ConvolutionIntegerSegment>,
    int64_t width,int64_t height,std::array<int32_t,2> cell); //7a4270
std::vector<ConvolutionIntegerSegment> escherBoundaryCloseTileEdges(std::span<const ConvolutionIntegerSegment>,
    int64_t width,int64_t height,std::array<int32_t,2> cell); //7a45e0
std::vector<EscherBoundaryRow> escherBoundaryFilledRows(std::span<const EscherBoundaryTileClassification>,
    std::span<const std::array<int32_t,2>>); //7a77e0
ConvolutionIntegerPolygon escherBoundaryTraceTileEdges(std::span<const ConvolutionIntegerSegment>); //855650 ->855870, signed multiplicities
struct EscherBoundaryTiledPolygon {
    std::vector<ConvolutionIntegerPolygon> partials;
    std::vector<std::array<int32_t,2>> cells;
    std::vector<EscherBoundaryRow> rows;
};
EscherBoundaryTiledPolygon escherBoundaryTilePolygon(const ConvolutionIntegerPolygon &,int64_t width,int64_t height); //7a8800
}
