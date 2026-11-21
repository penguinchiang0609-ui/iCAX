#pragma once
#include "recovered_escher_boundary_geometry.hpp"
namespace study::recovered {
struct EscherBoundaryIncidentDirection {
    int64_t x=0,y=0;
    uint8_t outgoing=0;
    std::array<uint8_t,7> padding{};
    bool operator==(const EscherBoundaryIncidentDirection &)const=default;
};
static_assert(sizeof(EscherBoundaryIncidentDirection)==24);
std::vector<EscherBoundaryIncidentDirection> escherBoundaryIncidentDirections(
    std::span<const ConvolutionIntegerSegment>,EscherIntegerPoint); //7a59b0
bool escherBoundaryTilePointSide(std::span<const ConvolutionIntegerSegment>,EscherIntegerPoint,bool); //7a6210
EscherIntegerBox escherBoundaryRotateTileBox(EscherIntegerBox); //7a2b00
int32_t escherBoundaryTileSide(std::span<const ConvolutionIntegerSegment>,EscherIntegerBox); //7a6330
struct EscherBoundaryTileClassification {
    std::array<int32_t,4> sides{};
    uint8_t inside=0,outside=0;
    std::array<uint8_t,2> padding{};
    bool operator==(const EscherBoundaryTileClassification &)const=default;
};
static_assert(sizeof(EscherBoundaryTileClassification)==20);
EscherBoundaryTileClassification escherBoundaryClassifyTile(std::span<const ConvolutionIntegerSegment>,EscherIntegerBox); //7a6730
std::vector<std::array<int32_t,2>> escherBoundaryTileCoordinates(std::span<const std::array<int32_t,2>>,
    std::span<const EscherBoundaryTileClassification>); //7a8610
struct EscherBoundaryRun {int32_t lower=0,lowerKind=0,upper=0,upperKind=0;bool operator==(const EscherBoundaryRun &)const=default;};
struct EscherBoundaryRow {int32_t column=0;std::vector<EscherBoundaryRun> runs;};
struct EscherBoundaryColumnEntry {EscherBoundaryTileClassification status;int32_t position=0;};
static_assert(sizeof(EscherBoundaryColumnEntry)==24);
std::vector<EscherBoundaryRun> escherBoundaryColumnRuns(std::span<const EscherBoundaryColumnEntry>); //7a6d30
std::array<std::vector<int32_t>,2> escherBoundaryFlattenRows(std::span<const EscherBoundaryRow>); //7aaf00
}
