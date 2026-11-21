#pragma once
#include "recovered_general_tiling_supplemental.hpp"
namespace study::recovered {
PlacementOrientation generalTilingSupplementalOrientation(PlacementOrientation,bool symmetryX,bool symmetryY); //3731e0
EscherPatternRecord generalTilingSupplementalRecord(const TilingMember &,
    const EscherIntegerShapeRecord &,double scale,bool symmetryX,bool symmetryY); //4947f0
std::optional<EscherPreparedSource> generalTilingSupplementalSource(const TilingPattern &,
    const EscherIntegerShapeRecord &,double scale,bool symmetryX,bool symmetryY); //494a20 record assembly
bool generalTilingAppendSupplementalPattern(EscherGeometryCatalog &,const TilingPattern &,
    const EscherIntegerShapeRecord &,double scale,bool symmetryX,bool symmetryY); //494a20
}
