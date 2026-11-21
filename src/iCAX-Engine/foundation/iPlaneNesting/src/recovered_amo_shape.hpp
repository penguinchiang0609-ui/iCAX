#pragma once
#include "recovered_amo_tiling.hpp"
namespace study::recovered {
using AmoTilingMatrix=std::array<double,7>;
struct AmoTilingShapeProperties {
    uint64_t field50=0;
    uint8_t field58=0,flag59=0,flag5a=0;
    Point flaggedPoint{};
};
struct AmoTilingTransformedShape {
    AmoTilingShape geometry;
    AmoTilingShapeProperties properties;
};
void amoTilingComposeMatrix(AmoTilingMatrix &,const AmoTilingMatrix &); //b7bc40
Point amoTilingTransformPoint(Point,const AmoTilingMatrix &); //8aa030
AmoTilingMatrix amoTilingReverseMatrix(double angle=0); //8b9840 matrix construction
AmoTilingTransformedShape amoTilingTransformShape(const AmoTilingShape &,const AmoTilingMatrix &,
    const AmoTilingShapeProperties &properties={}); //8b9d30
}
