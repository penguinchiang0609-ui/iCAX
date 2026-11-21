#pragma once
#include "recovered_convolution_topology.hpp"
namespace study::recovered {
std::vector<ShakerPoint> convolutionTopologyPoints(const ConvolutionTopology &); // 853c00
void convolutionScalePolygon(std::span<ShakerPoint>,double); // 7f63a0
void convolutionRotatePolygon(std::span<ShakerPoint>,int32_t angle10000Degrees); // 7f6120
void convolutionAppendPolygonRecords(std::span<const ShakerPoint>,std::vector<ShakerBoundary> &); // 721970
std::vector<ShakerBoundary> convolutionPolygonRecords(std::span<const std::vector<ShakerPoint>>); // 721c30
using ConvolutionRing=std::vector<ShakerPoint>;
using ConvolutionPolygon=std::vector<ConvolutionRing>;
using ConvolutionMultiPolygon=std::vector<ConvolutionPolygon>;
struct ConvolutionScale {ShakerPoint factors{};std::vector<ShakerPoint> references;};
ShakerPoint convolutionFirstPoint(const ConvolutionMultiPolygon &); // 7f0600
std::array<double,4> convolutionMultiBounds(const ConvolutionMultiPolygon &); // 7eea40, nonempty first ring
ShakerPoint convolutionChooseScale(std::span<const ConvolutionMultiPolygon>,int64_t limit); // 7f07a0
ConvolutionScale convolutionScaleConfig(std::span<const ConvolutionMultiPolygon>,int64_t limit,bool isotropic); // 7f04a0/7eeb00
void convolutionApplyScale(ConvolutionMultiPolygon &,ShakerPoint,std::span<const ShakerPoint>); // 7eee00
using ConvolutionIntegerPoint=std::array<int64_t,2>;
std::vector<ConvolutionIntegerPoint> convolutionCleanIntegerRing(std::span<const ConvolutionIntegerPoint>); // 7efa90
std::array<double,4> convolutionPolygonBounds(const ConvolutionPolygon &); // 57d6b0
std::vector<ConvolutionIntegerPoint> convolutionRoundRing(std::span<const ShakerPoint>); // 39af70
std::vector<ConvolutionIntegerPoint> convolutionQuantizeRing(std::span<const ShakerPoint>); // 7ecdf0
using ConvolutionIntegerRing=std::vector<ConvolutionIntegerPoint>;
using ConvolutionIntegerPolygon=std::vector<ConvolutionIntegerRing>;
using ConvolutionIntegerMultiPolygon=std::vector<ConvolutionIntegerPolygon>;
ConvolutionIntegerMultiPolygon convolutionQuantizeNested(const ConvolutionMultiPolygon &); // 7edfd0
ConvolutionIntegerMultiPolygon convolutionCleanNested(const ConvolutionIntegerMultiPolygon &); // 7efc40
std::vector<ConvolutionIntegerMultiPolygon> convolutionQuantizeOperands(std::vector<ConvolutionMultiPolygon> &,const ConvolutionScale &); // 7efe30 consumes input
ConvolutionPolygon convolutionDequantizePolygon(const ConvolutionIntegerPolygon &); // 7ee3c0, integer-to-double only
}
